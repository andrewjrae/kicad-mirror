/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2021 Andrew Lutsenko, anlutsenko at gmail dot com
 * Copyright (C) 1992-2022 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// kicad_curl.h *must be* included before any wxWidgets header to avoid conflicts
// at least on Windows/msys2
#include "kicad_curl/kicad_curl_easy.h"
#include <kicad_curl/kicad_curl.h>

#include "core/wx_stl_compat.h"
#include "kicad_build_version.h"
#include "paths.h"
#include "pcm.h"
#include "pgm_base.h"
#include "picosha2.h"
#include "settings/settings_manager.h"
#include <wx_filename.h>

#include <fstream>
#include <iomanip>
#include <memory>
#include <wx/dir.h>
#include <wx/filefn.h>
#include <wx/fs_zip.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/sstream.h>
#include <wx/tokenzr.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>


const std::tuple<int, int, int> PLUGIN_CONTENT_MANAGER::m_kicad_version =
        KICAD_MAJOR_MINOR_PATCH_TUPLE;


class THROWING_ERROR_HANDLER : public nlohmann::json_schema::error_handler
{
    void error( const json::json_pointer& ptr, const json& instance,
                const std::string& message ) override
    {
        throw std::invalid_argument( std::string( "At " ) + ptr.to_string() + ", value:\n"
                                     + instance.dump() + "\n" + message + "\n" );
    }
};


class STATUS_TEXT_REPORTER : public PROGRESS_REPORTER_BASE
{
public:
    STATUS_TEXT_REPORTER( std::function<void( const wxString )> aStatusCallback ) :
            PROGRESS_REPORTER_BASE( 1 ), m_statusCallback( aStatusCallback )
    {
    }

    void SetTitle( const wxString& aTitle ) override
    {
        m_title = aTitle;
        m_report = wxT( "" );
    }

    void Report( const wxString& aMessage ) override
    {
        m_report = wxString::Format( ": %s", aMessage );
    }

    void Cancel() { m_cancelled.store( true ); }

private:
    bool updateUI() override
    {
        m_statusCallback( wxString::Format( "%s%s", m_title, m_report ) );
        return true;
    }

    const std::function<void( const wxString )> m_statusCallback;

    wxString m_title;
    wxString m_report;
};


PLUGIN_CONTENT_MANAGER::PLUGIN_CONTENT_MANAGER(
        std::function<void( int )>            aAvailableUpdateCallback,
        std::function<void( const wxString )> aStatusCallback ) :
        m_dialog( nullptr ),
        m_availableUpdateCallback( aAvailableUpdateCallback ), m_statusCallback( aStatusCallback )
{
    ReadEnvVar();

    // Read and store pcm schema
    wxFileName schema_file( PATHS::GetStockDataPath( true ), "pcm.v1.schema.json" );
    schema_file.Normalize( FN_NORMALIZE_FLAGS | wxPATH_NORM_ENV_VARS );
    schema_file.AppendDir( "schemas" );

    std::ifstream  schema_stream( schema_file.GetFullPath().ToUTF8() );
    nlohmann::json schema;

    try
    {
        schema_stream >> schema;
        m_schema_validator.set_root_schema( schema );
    }
    catch( std::exception& e )
    {
        if( !schema_file.FileExists() )
            wxLogError( wxString::Format( _( "schema file '%s' not found" ),
                                          schema_file.GetFullPath() ) );
        else
            wxLogError( wxString::Format( _( "Error loading schema: %s" ), e.what() ) );
    }

    // Load currently installed packages
    wxFileName f( SETTINGS_MANAGER::GetUserSettingsPath(), "installed_packages.json" );

    if( f.FileExists() )
    {
        std::ifstream  installed_stream( f.GetFullPath().ToUTF8() );
        nlohmann::json installed;

        try
        {
            installed_stream >> installed;

            if( installed.contains( "packages" ) && installed["packages"].is_array() )
            {
                for( const auto& js_entry : installed["packages"] )
                {
                    PCM_INSTALLATION_ENTRY entry = js_entry.get<PCM_INSTALLATION_ENTRY>();
                    m_installed.emplace( entry.package.identifier, entry );
                }
            }
        }
        catch( std::exception& e )
        {
            wxLogError( wxString::Format( _( "Error loading installed packages list: %s" ),
                                          e.what() ) );
        }
    }

    // As a fall back populate installed from names of directories

    for( const wxString& dir : PCM_PACKAGE_DIRECTORIES )
    {
        wxFileName d( m_3rdparty_path, "" );
        d.AppendDir( dir );

        if( d.DirExists() )
        {
            wxDir package_dir( d.GetPath() );

            if( !package_dir.IsOpened() )
                continue;

            wxString subdir;
            bool     more = package_dir.GetFirst( &subdir, "", wxDIR_DIRS | wxDIR_HIDDEN );

            while( more )
            {
                wxString actual_package_id = subdir;
                actual_package_id.Replace( '_', '.' );

                if( m_installed.find( actual_package_id ) == m_installed.end() )
                {
                    PCM_INSTALLATION_ENTRY entry;
                    wxFileName             subdir_file( d.GetPath(), subdir );

                    // wxFileModificationTime bugs out on windows for directories
                    wxStructStat stat;
                    int          stat_code = wxStat( subdir_file.GetFullPath(), &stat );

                    entry.package.name = subdir;
                    entry.package.identifier = actual_package_id;
                    entry.current_version = "0.0";
                    entry.repository_name = wxT( "<unknown>" );

                    if( stat_code == 0 )
                        entry.install_timestamp = stat.st_mtime;

                    PACKAGE_VERSION version;
                    version.version = "0.0";
                    version.status = PVS_STABLE;
                    version.kicad_version = KICAD_MAJOR_MINOR_VERSION;

                    entry.package.versions.emplace_back( version );

                    m_installed.emplace( actual_package_id, entry );
                }

                more = package_dir.GetNext( &subdir );
            }
        }
    }

    // Calculate package compatibility
    std::for_each( m_installed.begin(), m_installed.end(),
                   [&]( auto& entry )
                   {
                       preparePackage( entry.second.package );
                   } );
}


void PLUGIN_CONTENT_MANAGER::ReadEnvVar()
{
    // Get 3rd party path
    const ENV_VAR_MAP& env = Pgm().GetLocalEnvVariables();
    auto               it = env.find( "KICAD6_3RD_PARTY" );

    if( it != env.end() && !it->second.GetValue().IsEmpty() )
        m_3rdparty_path = it->second.GetValue();
    else
        m_3rdparty_path = PATHS::GetDefault3rdPartyPath();
}


bool PLUGIN_CONTENT_MANAGER::DownloadToStream( const wxString& aUrl, std::ostream* aOutput,
                                               PROGRESS_REPORTER* aReporter,
                                               const size_t       aSizeLimit )
{
    bool size_exceeded = false;

    TRANSFER_CALLBACK callback = [&]( size_t dltotal, size_t dlnow, size_t ultotal, size_t ulnow )
    {
        if( aSizeLimit > 0 && ( dltotal > aSizeLimit || dlnow > aSizeLimit ) )
        {
            size_exceeded = true;

            // Non zero return means abort.
            return true;
        }

        if( dltotal > 1000 )
        {
            aReporter->SetCurrentProgress( dlnow / (double) dltotal );
            aReporter->Report( wxString::Format( _( "Downloading %lld/%lld kB" ), dlnow / 1000,
                                                 dltotal / 1000 ) );
        }
        else
        {
            aReporter->SetCurrentProgress( 0.0 );
        }

        return !aReporter->KeepRefreshing();
    };

    KICAD_CURL_EASY curl;
    curl.SetOutputStream( aOutput );
    curl.SetURL( aUrl.ToUTF8().data() );
    curl.SetFollowRedirects( true );
    curl.SetTransferCallback( callback, 250000L );

    int code = curl.Perform();

    if( !aReporter->IsCancelled() )
        aReporter->SetCurrentProgress( 1.0 );

    if( code != CURLE_OK )
    {
        if( m_dialog )
        {
            if( code == CURLE_ABORTED_BY_CALLBACK && size_exceeded )
                wxMessageBox( _( "Download is too large." ) );
            else if( code != CURLE_ABORTED_BY_CALLBACK )
                wxLogError( wxString( curl.GetErrorText( code ) ) );
        }

        return false;
    }

    return true;
}


bool PLUGIN_CONTENT_MANAGER::FetchRepository( const wxString& aUrl, PCM_REPOSITORY& aRepository,
                                              PROGRESS_REPORTER* aReporter )
{
    std::stringstream repository_stream;

    aReporter->SetTitle( _( "Fetching repository" ) );

    if( !DownloadToStream( aUrl, &repository_stream, aReporter, 20480 ) )
    {
        if( m_dialog )
            wxLogError( _( "Unable to load repository url" ) );

        return false;
    }

    nlohmann::json repository_json;

    try
    {
        repository_stream >> repository_json;

        ValidateJson( repository_json, nlohmann::json_uri( "#/definitions/Repository" ) );

        aRepository = repository_json.get<PCM_REPOSITORY>();
    }
    catch( const std::exception& e )
    {
        if( m_dialog )
            wxLogError( wxString::Format( _( "Unable to parse repository:\n\n%s" ), e.what() ) );

        return false;
    }

    return true;
}


void PLUGIN_CONTENT_MANAGER::ValidateJson( const nlohmann::json&     aJson,
                                           const nlohmann::json_uri& aUri ) const
{
    THROWING_ERROR_HANDLER error_handler;
    m_schema_validator.validate( aJson, error_handler, aUri );
}


bool PLUGIN_CONTENT_MANAGER::fetchPackages( const wxString&                aUrl,
                                            const std::optional<wxString>& aHash,
                                            std::vector<PCM_PACKAGE>&      aPackages,
                                            PROGRESS_REPORTER*             aReporter )
{
    std::stringstream packages_stream;

    aReporter->SetTitle( _( "Fetching repository packages" ) );

    if( !DownloadToStream( aUrl, &packages_stream, aReporter ) )
    {
        if( m_dialog )
            wxLogError( _( "Unable to load repository packages url." ) );

        return false;
    }

    std::istringstream isstream( packages_stream.str() );

    if( aHash && !VerifyHash( isstream, *aHash ) )
    {
        if( m_dialog )
            wxLogError( _( "Packages hash doesn't match. Repository may be corrupted." ) );

        return false;
    }

    try
    {
        nlohmann::json packages_json = nlohmann::json::parse( packages_stream.str() );
        ValidateJson( packages_json, nlohmann::json_uri( "#/definitions/PackageArray" ) );

        aPackages = packages_json["packages"].get<std::vector<PCM_PACKAGE>>();
    }
    catch( std::exception& e )
    {
        if( m_dialog )
            wxLogError(
                    wxString::Format( _( "Unable to parse packages metadata:\n\n%s" ), e.what() ) );

        return false;
    }

    return true;
}


bool PLUGIN_CONTENT_MANAGER::VerifyHash( std::istream& aStream, const wxString& aHash ) const
{
    std::vector<unsigned char> bytes( picosha2::k_digest_size );

    picosha2::hash256( std::istreambuf_iterator<char>( aStream ), std::istreambuf_iterator<char>(),
                       bytes.begin(), bytes.end() );
    std::string hex_str = picosha2::bytes_to_hex_string( bytes.begin(), bytes.end() );

    return aHash.compare( hex_str ) == 0;
}


const PCM_REPOSITORY&
PLUGIN_CONTENT_MANAGER::getCachedRepository( const wxString& aRepositoryId ) const
{
    wxASSERT_MSG( m_repository_cache.find( aRepositoryId ) != m_repository_cache.end(),
                  "Repository is not cached." );

    return m_repository_cache.at( aRepositoryId );
}


const bool PLUGIN_CONTENT_MANAGER::CacheRepository( const wxString& aRepositoryId )
{
    if( m_repository_cache.find( aRepositoryId ) != m_repository_cache.end() )
        return true;

    const auto repository_tuple =
            std::find_if( m_repository_list.begin(), m_repository_list.end(),
                          [&aRepositoryId]( const std::tuple<wxString, wxString, wxString>& t )
                          {
                              return std::get<0>( t ) == aRepositoryId;
                          } );

    if( repository_tuple == m_repository_list.end() )
        return false;

    wxString url = std::get<2>( *repository_tuple );

    nlohmann::json js;
    PCM_REPOSITORY current_repo;

    std::shared_ptr<PROGRESS_REPORTER> reporter;

    if( m_dialog )
        reporter = std::make_shared<WX_PROGRESS_REPORTER>( m_dialog, wxT( "" ), 1 );
    else
        reporter = m_statusReporter;

    if( !FetchRepository( url, current_repo, reporter.get() ) )
        return false;

    bool packages_cache_exists = false;

    // First load repository data from local filesystem if available.
    wxFileName repo_cache = wxFileName( PATHS::GetUserCachePath(), "repository.json" );
    repo_cache.AppendDir( "pcm" );
    repo_cache.AppendDir( aRepositoryId );
    wxFileName packages_cache( repo_cache.GetPath(), "packages.json" );

    if( repo_cache.FileExists() && packages_cache.FileExists() )
    {
        std::ifstream repo_stream( repo_cache.GetFullPath().ToUTF8() );
        repo_stream >> js;
        PCM_REPOSITORY saved_repo = js.get<PCM_REPOSITORY>();

        if( saved_repo.packages.update_timestamp == current_repo.packages.update_timestamp )
        {
            // Cached repo is up to date, use data on disk
            js.clear();
            std::ifstream packages_cache_stream( packages_cache.GetFullPath().ToUTF8() );

            try
            {
                packages_cache_stream >> js;
                saved_repo.package_list = js["packages"].get<std::vector<PCM_PACKAGE>>();

                for( size_t i = 0; i < saved_repo.package_list.size(); i++ )
                {
                    preparePackage( saved_repo.package_list[i] );
                    saved_repo.package_map[saved_repo.package_list[i].identifier] = i;
                }

                m_repository_cache[aRepositoryId] = std::move( saved_repo );

                packages_cache_exists = true;
            }
            catch( ... )
            {
                if( m_dialog )
                    wxLogError( _( "Packages cache for current repository is "
                                   "corrupted, it will be redownloaded." ) );
            }
        }
    }

    if( !packages_cache_exists )
    {
        // Cache doesn't exist or is out of date
        if( !fetchPackages( current_repo.packages.url, current_repo.packages.sha256,
                            current_repo.package_list, reporter.get() ) )
        {
            return false;
        }

        for( size_t i = 0; i < current_repo.package_list.size(); i++ )
        {
            preparePackage( current_repo.package_list[i] );
            current_repo.package_map[current_repo.package_list[i].identifier] = i;
        }

        repo_cache.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

        std::ofstream repo_cache_stream( repo_cache.GetFullPath().ToUTF8() );
        repo_cache_stream << std::setw( 4 ) << nlohmann::json( current_repo ) << std::endl;

        std::ofstream packages_cache_stream( packages_cache.GetFullPath().ToUTF8() );
        js.clear();
        js["packages"] = nlohmann::json( current_repo.package_list );
        packages_cache_stream << std::setw( 4 ) << js << std::endl;

        m_repository_cache[aRepositoryId] = std::move( current_repo );
    }

    if( current_repo.resources )
    {
        // Check resources file date, redownload if needed
        PCM_RESOURCE_REFERENCE& resources = *current_repo.resources;

        wxFileName resource_file( repo_cache.GetPath(), "resources.zip" );

        time_t mtime = 0;

        if( resource_file.FileExists() )
            mtime = wxFileModificationTime( resource_file.GetFullPath() );

        if( mtime + 600 < getCurrentTimestamp() && mtime < (time_t) resources.update_timestamp )
        {
            std::ofstream resources_stream( resource_file.GetFullPath().ToUTF8(),
                                            std::ios_base::binary );

            reporter->SetTitle( _( "Downloading resources" ) );

            // 100 Mb resource file limit
            bool success = DownloadToStream( resources.url, &resources_stream, reporter.get(),
                                             100 * 1024 * 1024 );

            resources_stream.close();

            if( success )
            {
                std::ifstream read_stream( resource_file.GetFullPath().ToUTF8(),
                                           std::ios_base::binary );


                if( resources.sha256 && !VerifyHash( read_stream, *resources.sha256 ) )
                {
                    read_stream.close();

                    if( m_dialog )
                        wxLogError( _( "Resources file hash doesn't match and will not be used. "
                                       "Repository may be corrupted." ) );

                    wxRemoveFile( resource_file.GetFullPath() );
                }
            }
            else
            {
                // Not critical, just clean up the file
                wxRemoveFile( resource_file.GetFullPath() );
            }
        }
    }

    updateInstalledPackagesMetadata( aRepositoryId );

    return true;
}


void PLUGIN_CONTENT_MANAGER::updateInstalledPackagesMetadata( const wxString& aRepositoryId )
{
    const PCM_REPOSITORY* repository;

    try
    {
        repository = &getCachedRepository( aRepositoryId );
    }
    catch( ... )
    {
        wxLogDebug( "Invalid/Missing repository " + aRepositoryId );
        return;
    }

    for( auto& entry : m_installed )
    {
        PCM_INSTALLATION_ENTRY& installation_entry = entry.second;

        // If current package is not from this repository, skip it
        if( installation_entry.repository_id != aRepositoryId )
            continue;

        // If current package is no longer in this repository, keep it as is
        if( repository->package_map.count( installation_entry.package.identifier ) == 0 )
            continue;

        std::optional<PACKAGE_VERSION> current_version;

        auto current_version_it =
                std::find_if( installation_entry.package.versions.begin(),
                              installation_entry.package.versions.end(),
                              [&]( const PACKAGE_VERSION& version )
                              {
                                  return version.version == installation_entry.current_version;
                              } );

        if( current_version_it != installation_entry.package.versions.end() )
            current_version = *current_version_it; // copy

        // Copy repository metadata into installation entry
        installation_entry.package = repository->package_list[repository->package_map.at(
                installation_entry.package.identifier )];

        // Insert current version if it's missing from repository metadata
        current_version_it =
                std::find_if( installation_entry.package.versions.begin(),
                              installation_entry.package.versions.end(),
                              [&]( const PACKAGE_VERSION& version )
                              {
                                  return version.version == installation_entry.current_version;
                              } );

        if( current_version_it == installation_entry.package.versions.end() )
        {
            installation_entry.package.versions.emplace_back( *current_version );

            // Re-sort the versions by descending version
            std::sort( installation_entry.package.versions.begin(),
                       installation_entry.package.versions.end(),
                       []( const PACKAGE_VERSION& a, const PACKAGE_VERSION& b )
                       {
                           return a.parsed_version > b.parsed_version;
                       } );
        }
    }
}


void PLUGIN_CONTENT_MANAGER::preparePackage( PCM_PACKAGE& aPackage )
{
    // Parse package version strings
    for( PACKAGE_VERSION& ver : aPackage.versions )
    {
        int epoch = 0, major = 0, minor = 0, patch = 0;

        if( ver.version_epoch )
            epoch = *ver.version_epoch;

        wxStringTokenizer version_tokenizer( ver.version, "." );

        major = wxAtoi( version_tokenizer.GetNextToken() );

        if( version_tokenizer.HasMoreTokens() )
            minor = wxAtoi( version_tokenizer.GetNextToken() );

        if( version_tokenizer.HasMoreTokens() )
            patch = wxAtoi( version_tokenizer.GetNextToken() );

        ver.parsed_version = std::make_tuple( epoch, major, minor, patch );

        // Determine compatibility
        ver.compatible = true;

        auto parse_version_tuple = []( const wxString& version, int deflt )
        {
            int ver_major = deflt;
            int ver_minor = deflt;
            int ver_patch = deflt;

            wxStringTokenizer tokenizer( version, "." );

            ver_major = wxAtoi( tokenizer.GetNextToken() );

            if( tokenizer.HasMoreTokens() )
                ver_minor = wxAtoi( tokenizer.GetNextToken() );

            if( tokenizer.HasMoreTokens() )
                ver_patch = wxAtoi( tokenizer.GetNextToken() );

            return std::tuple<int, int, int>( ver_major, ver_minor, ver_patch );
        };

        if( parse_version_tuple( ver.kicad_version, 0 ) > m_kicad_version )
            ver.compatible = false;

        if( ver.kicad_version_max
            && parse_version_tuple( *ver.kicad_version_max, 999 ) < m_kicad_version )
            ver.compatible = false;

#ifdef __WXMSW__
        wxString platform = wxT( "windows" );
#endif
#ifdef __WXOSX__
        wxString platform = wxT( "macos" );
#endif
#ifdef __WXGTK__
        wxString platform = wxT( "linux" );
#endif

        if( ver.platforms.size() > 0
            && std::find( ver.platforms.begin(), ver.platforms.end(), platform )
                       == ver.platforms.end() )
        {
            ver.compatible = false;
        }
    }

    // Sort by descending version
    std::sort( aPackage.versions.begin(), aPackage.versions.end(),
               []( const PACKAGE_VERSION& a, const PACKAGE_VERSION& b )
               {
                   return a.parsed_version > b.parsed_version;
               } );
}


const std::vector<PCM_PACKAGE>&
PLUGIN_CONTENT_MANAGER::GetRepositoryPackages( const wxString& aRepositoryId ) const
{
    static std::vector<PCM_PACKAGE> empty{};

    try
    {
        return getCachedRepository( aRepositoryId ).package_list;
    }
    catch( ... )
    {
        return empty;
    }
}


void PLUGIN_CONTENT_MANAGER::SetRepositoryList( const STRING_PAIR_LIST& aRepositories )
{
    // Clean up cache folder if repository is not in new list
    for( const auto& entry : m_repository_list )
    {
        auto it = std::find_if( aRepositories.begin(), aRepositories.end(),
                                [&]( const auto& new_entry )
                                {
                                    return new_entry.first == std::get<1>( entry );
                                } );

        if( it == aRepositories.end() )
        {
            DiscardRepositoryCache( std::get<0>( entry ) );
        }
    }

    m_repository_list.clear();
    m_repository_cache.clear();

    for( const auto& repo : aRepositories )
    {
        std::string url_sha = picosha2::hash256_hex_string( repo.second );
        m_repository_list.push_back(
                std::make_tuple( url_sha.substr( 0, 16 ), repo.first, repo.second ) );
    }
}


void PLUGIN_CONTENT_MANAGER::DiscardRepositoryCache( const wxString& aRepositoryId )
{
    if( m_repository_cache.count( aRepositoryId ) > 0 )
        m_repository_cache.erase( aRepositoryId );

    wxFileName repo_cache = wxFileName( PATHS::GetUserCachePath(), "" );
    repo_cache.AppendDir( "pcm" );
    repo_cache.AppendDir( aRepositoryId );

    if( repo_cache.DirExists() )
        repo_cache.Rmdir( wxPATH_RMDIR_RECURSIVE );
}


void PLUGIN_CONTENT_MANAGER::MarkInstalled( const PCM_PACKAGE& aPackage, const wxString& aVersion,
                                            const wxString& aRepositoryId )
{
    // In case of package update remove old data
    MarkUninstalled( aPackage );

    PCM_INSTALLATION_ENTRY entry;
    entry.package = aPackage;
    entry.current_version = aVersion;
    entry.repository_id = aRepositoryId;

    try
    {
        if( !aRepositoryId.IsEmpty() )
            entry.repository_name = getCachedRepository( aRepositoryId ).name;
        else
            entry.repository_name = _( "Local file" );
    }
    catch( ... )
    {
        entry.repository_name = _( "Unknown" );
    }

    entry.install_timestamp = getCurrentTimestamp();

    m_installed.emplace( aPackage.identifier, entry );
}


void PLUGIN_CONTENT_MANAGER::MarkUninstalled( const PCM_PACKAGE& aPackage )
{
    m_installed.erase( aPackage.identifier );
}


PCM_PACKAGE_STATE PLUGIN_CONTENT_MANAGER::GetPackageState( const wxString& aRepositoryId,
                                                           const wxString& aPackageId )
{
    bool installed = m_installed.find( aPackageId ) != m_installed.end();

    if( aRepositoryId.IsEmpty() || !CacheRepository( aRepositoryId ) )
        return installed ? PPS_INSTALLED : PPS_UNAVAILABLE;

    const PCM_REPOSITORY* repo;

    try
    {
        repo = &getCachedRepository( aRepositoryId );
    }
    catch( ... )
    {
        return installed ? PPS_INSTALLED : PPS_UNAVAILABLE;
    }

    if( repo->package_map.count( aPackageId ) == 0 )
        return installed ? PPS_INSTALLED : PPS_UNAVAILABLE;

    const PCM_PACKAGE& pkg = repo->package_list[repo->package_map.at( aPackageId )];

    if( installed )
    {
        // Package is installed, check for available updates at the same or
        // higher (numerically lower) version stability level
        wxString update_version = GetPackageUpdateVersion( pkg );

        return update_version.IsEmpty() ? PPS_INSTALLED : PPS_UPDATE_AVAILABLE;
    }
    else
    {
        // Find any compatible version
        auto ver_it = std::find_if( pkg.versions.begin(), pkg.versions.end(),
                                    []( const PACKAGE_VERSION& ver )
                                    {
                                        return ver.compatible;
                                    } );

        return ver_it == pkg.versions.end() ? PPS_UNAVAILABLE : PPS_AVAILABLE;
    }
}


const wxString PLUGIN_CONTENT_MANAGER::GetPackageUpdateVersion( const PCM_PACKAGE& aPackage )
{
    wxASSERT_MSG( m_installed.find( aPackage.identifier ) != m_installed.end(),
                  "GetPackageUpdateVersion called on a not installed package" );

    const PCM_INSTALLATION_ENTRY& installation_entry = m_installed.at( aPackage.identifier );

    auto installed_ver_it = std::find_if(
            installation_entry.package.versions.begin(), installation_entry.package.versions.end(),
            [&]( const PACKAGE_VERSION& ver )
            {
                return ver.version == installation_entry.current_version;
            } );

    wxASSERT_MSG( installed_ver_it != installation_entry.package.versions.end(),
                  "Installed package version not found" );

    auto ver_it = std::find_if( aPackage.versions.begin(), aPackage.versions.end(),
                                [&]( const PACKAGE_VERSION& ver )
                                {
                                    return ver.compatible && installed_ver_it->status >= ver.status
                                           && installed_ver_it->parsed_version < ver.parsed_version;
                                } );

    return ver_it == aPackage.versions.end() ? wxT( "" ) : ver_it->version;
}

time_t PLUGIN_CONTENT_MANAGER::getCurrentTimestamp() const
{
    return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch() )
            .count();
}


void PLUGIN_CONTENT_MANAGER::SaveInstalledPackages()
{
    try
    {
        nlohmann::json js;
        js["packages"] = nlohmann::json::array();

        for( const auto& entry : m_installed )
        {
            js["packages"].emplace_back( entry.second );
        }

        wxFileName    f( SETTINGS_MANAGER::GetUserSettingsPath(), "installed_packages.json" );
        std::ofstream stream( f.GetFullPath().ToUTF8() );

        stream << std::setw( 4 ) << js << std::endl;
    }
    catch( nlohmann::detail::exception& )
    {
        // Ignore
    }
}


const std::vector<PCM_INSTALLATION_ENTRY> PLUGIN_CONTENT_MANAGER::GetInstalledPackages() const
{
    std::vector<PCM_INSTALLATION_ENTRY> v;

    std::for_each( m_installed.begin(), m_installed.end(),
                   [&v]( const auto& entry )
                   {
                       v.push_back( entry.second );
                   } );

    std::sort( v.begin(), v.end(),
               []( const PCM_INSTALLATION_ENTRY& a, const PCM_INSTALLATION_ENTRY& b )
               {
                   return ( a.install_timestamp < b.install_timestamp )
                          || ( a.install_timestamp == b.install_timestamp
                               && a.package.identifier < b.package.identifier );
               } );

    return v;
}


const wxString&
PLUGIN_CONTENT_MANAGER::GetInstalledPackageVersion( const wxString& aPackageId ) const
{
    wxASSERT_MSG( m_installed.find( aPackageId ) != m_installed.end(),
                  "Installed package not found." );

    return m_installed.at( aPackageId ).current_version;
}


bool PLUGIN_CONTENT_MANAGER::IsPackagePinned( const wxString& aPackageId ) const
{
    if( m_installed.find( aPackageId ) == m_installed.end() )
        return false;

    return m_installed.at( aPackageId ).pinned;
}


void PLUGIN_CONTENT_MANAGER::SetPinned( const wxString& aPackageId, const bool aPinned )
{
    if( m_installed.find( aPackageId ) == m_installed.end() )
        return;

    m_installed.at( aPackageId ).pinned = aPinned;
}


int PLUGIN_CONTENT_MANAGER::GetPackageSearchRank( const PCM_PACKAGE& aPackage,
                                                  const wxString&    aSearchTerm )
{
    wxArrayString terms = wxStringTokenize( aSearchTerm.Lower(), " ", wxTOKEN_STRTOK );
    int           rank = 0;

    const auto find_term_matches = [&]( const wxString& str )
    {
        int      result = 0;
        wxString lower = str.Lower();

        for( const wxString& term : terms )
            if( lower.Find( term ) != wxNOT_FOUND )
                result += 1;

        return result;
    };

    // Match on package id
    if( terms.size() == 1 && terms[0] == aPackage.identifier )
        rank += 10000;

    if( terms.size() == 1 && find_term_matches( aPackage.identifier ) )
        rank += 1000;

    // Match on package name
    rank += 500 * find_term_matches( aPackage.name );

    // Match on tags
    for( const std::string& tag : aPackage.tags )
        rank += 100 * find_term_matches( wxString( tag ) );

    // Match on package description
    rank += 10 * find_term_matches( aPackage.description );
    rank += 10 * find_term_matches( aPackage.description_full );

    // Match on author/maintainer
    rank += find_term_matches( aPackage.author.name );

    if( aPackage.maintainer )
        rank += 3 * find_term_matches( aPackage.maintainer->name );

    // Match on resources
    for( const auto& entry : aPackage.resources )
    {
        rank += find_term_matches( entry.first );
        rank += find_term_matches( entry.second );
    }

    // Match on license
    if( terms.size() == 1 && terms[0] == aPackage.license )
        rank += 1;

    return rank;
}


std::unordered_map<wxString, wxBitmap>
PLUGIN_CONTENT_MANAGER::GetRepositoryPackageBitmaps( const wxString& aRepositoryId )
{
    std::unordered_map<wxString, wxBitmap> bitmaps;

    wxFileName resources_file = wxFileName( PATHS::GetUserCachePath(), "resources.zip" );
    resources_file.AppendDir( "pcm" );
    resources_file.AppendDir( aRepositoryId );

    if( !resources_file.FileExists() )
        return bitmaps;

    wxFFileInputStream stream( resources_file.GetFullPath() );
    wxZipInputStream   zip( stream );

    if( !zip.IsOk() || zip.GetTotalEntries() == 0 )
        return bitmaps;

    for( wxArchiveEntry* entry = zip.GetNextEntry(); entry; entry = zip.GetNextEntry() )
    {
        wxArrayString path_parts =
                wxSplit( entry->GetName(), wxFileName::GetPathSeparator(), (wxChar) 0 );

        if( path_parts.size() != 2 || path_parts[1] != "icon.png" )
            continue;

        try
        {
            wxMemoryInputStream image_stream( zip, entry->GetSize() );
            wxImage             image( image_stream, wxBITMAP_TYPE_PNG );
            bitmaps.emplace( path_parts[0], wxBitmap( image ) );
        }
        catch( ... )
        {
            // Log and ignore
            wxLogTrace( "Error loading png bitmap for entry %s from %s", entry->GetName(),
                        resources_file.GetFullPath() );
        }
    }

    return bitmaps;
}


std::unordered_map<wxString, wxBitmap> PLUGIN_CONTENT_MANAGER::GetInstalledPackageBitmaps()
{
    std::unordered_map<wxString, wxBitmap> bitmaps;

    wxFileName resources_dir_fn( m_3rdparty_path, "" );
    resources_dir_fn.AppendDir( "resources" );
    wxDir resources_dir( resources_dir_fn.GetPath() );

    if( !resources_dir.IsOpened() )
        return bitmaps;

    wxString subdir;
    bool     more = resources_dir.GetFirst( &subdir, "", wxDIR_DIRS | wxDIR_HIDDEN );

    while( more )
    {
        wxFileName icon( resources_dir_fn.GetPath(), "icon.png" );
        icon.AppendDir( subdir );

        if( icon.FileExists() )
        {
            wxString actual_package_id = subdir;
            actual_package_id.Replace( '_', '.' );

            try
            {
                wxBitmap bitmap( icon.GetFullPath(), wxBITMAP_TYPE_PNG );
                bitmaps.emplace( actual_package_id, bitmap );
            }
            catch( ... )
            {
                // Log and ignore
                wxLogTrace( "Error loading png bitmap from %s", icon.GetFullPath() );
            }
        }

        more = resources_dir.GetNext( &subdir );
    }

    return bitmaps;
}


void PLUGIN_CONTENT_MANAGER::RunBackgroundUpdate()
{
    // If the thread is already running don't create it again
    if( m_updateThread.joinable() )
        return;

    m_statusReporter = std::make_shared<STATUS_TEXT_REPORTER>( m_statusCallback );

    m_updateThread = std::thread(
            [this]()
            {
                if( m_installed.size() == 0 )
                    return;

                // Only fetch repositories that have installed not pinned packages
                std::unordered_set<wxString> repo_ids;

                for( auto& entry : m_installed )
                {
                    if( !entry.second.pinned )
                        repo_ids.insert( entry.second.repository_id );
                }

                for( const auto& entry : m_repository_list )
                {
                    const wxString& repository_id = std::get<0>( entry );

                    if( repo_ids.count( repository_id ) == 0 )
                        continue;

                    CacheRepository( repository_id );

                    if( m_statusReporter->IsCancelled() )
                        break;
                }

                if( m_statusReporter->IsCancelled() )
                    return;

                // Count packages with updates
                int availableUpdateCount = 0;

                for( auto& entry : m_installed )
                {
                    PCM_INSTALLATION_ENTRY& installed_package = entry.second;

                    if( m_repository_cache.find( installed_package.repository_id )
                        != m_repository_cache.end() )
                    {
                        PCM_PACKAGE_STATE state =
                                GetPackageState( installed_package.repository_id,
                                                 installed_package.package.identifier );

                        if( state == PPS_UPDATE_AVAILABLE && !installed_package.pinned )
                            availableUpdateCount++;
                    }

                    if( m_statusReporter->IsCancelled() )
                        return;
                }

                // Update the badge on PCM button
                m_availableUpdateCallback( availableUpdateCount );

                m_statusCallback( availableUpdateCount > 0 ? _( "Package updates are available" )
                                                           : _( "No package updates available" ) );
            } );
}


void PLUGIN_CONTENT_MANAGER::StopBackgroundUpdate()
{
    if( m_updateThread.joinable() )
    {
        m_statusReporter->Cancel();
        m_updateThread.join();
    }
}


PLUGIN_CONTENT_MANAGER::~PLUGIN_CONTENT_MANAGER()
{
    // By the time object is being destroyed the thread should be
    // stopped already but just in case do it here too.
    StopBackgroundUpdate();
}
