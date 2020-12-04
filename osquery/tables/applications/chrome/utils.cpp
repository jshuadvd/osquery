/**
 * Copyright (c) 2014-present, The osquery authors
 *
 * This source code is licensed as defined by the LICENSE file found in the
 * root directory of this source tree.
 *
 * SPDX-License-Identifier: (Apache-2.0 OR GPL-2.0-only)
 */

#include <boost/algorithm/string/join.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <osquery/hashing/hashing.h>
#include <osquery/logger/logger.h>
#include <osquery/tables/applications/chrome/utils.h>
#include <osquery/tables/system/system_utils.h>
#include <osquery/utils/conversions/tryto.h>
#include <osquery/utils/info/platform_type.h>

namespace osquery {

namespace tables {

namespace {

/// The prefix that identifies localized strings
const std::string kLocalizedMessagePrefix{"__MSG_"};

/// The Preferences file included in each profile
const std::string kProfilePreferencesFile = "Preferences";

/// The alternative 'Secure Preferences' file included in each profile
const std::string kSecureProfilePreferencesFile = "Secure Preferences";

/// The possible configuration file names
const std::vector<std::reference_wrapper<const std::string>>
    kPossibleConfigFileNames = {std::ref(kProfilePreferencesFile),
                                std::ref(kSecureProfilePreferencesFile)};

/// A list of possible path suffixes for each browser type
using ChromePathSuffixMap =
    std::vector<std::tuple<ChromeBrowserType, std::string>>;

// clang-format off
const ChromePathSuffixMap kWindowsPathList = {
    {ChromeBrowserType::GoogleChrome, "AppData\\Local\\Google\\Chrome\\User Data"},
    {ChromeBrowserType::Brave, "AppData\\Roaming\\brave"},
    {ChromeBrowserType::Chromium, "AppData\\Local\\Chromium"},
    {ChromeBrowserType::Yandex, "AppData\\Local\\Yandex\\YandexBrowser\\User Data"},
    {ChromeBrowserType::Edge, "AppData\\Local\\Microsoft\\Edge\\User Data"},
    {ChromeBrowserType::EdgeBeta, "AppData\\Local\\Microsoft\\Edge Beta\\User Data"},
    {ChromeBrowserType::Opera, "AppData\\Roaming\\Opera Software\\Opera Stable"}};
// clang-format on

// clang-format off
const ChromePathSuffixMap kMacOsPathList = {
    {ChromeBrowserType::GoogleChrome, "Library/Application Support/Google/Chrome"},
    {ChromeBrowserType::Brave, "Library/Application Support/BraveSoftware/Brave-Browser"},
    {ChromeBrowserType::Chromium, "Library/Application Support/Chromium"},
    {ChromeBrowserType::Yandex, "Library/Application Support/Yandex/YandexBrowser"},
    {ChromeBrowserType::Opera, "Library/Application Support/com.operasoftware.Opera"}};
// clang-format on

const ChromePathSuffixMap kLinuxPathList = {
    {ChromeBrowserType::GoogleChrome, ".config/google-chrome"},
    {ChromeBrowserType::Brave, ".config/BraveSoftware/Brave-Browser"},
    {ChromeBrowserType::Chromium, ".config/chromium"},
    {ChromeBrowserType::Chromium, "snap/chromium/common/chromium"},
    {ChromeBrowserType::Yandex, ".config/yandex-browser-beta"},
    {ChromeBrowserType::Opera, ".config/opera"},
};

/// Maps ChromeBrowserType values to readable strings
const std::unordered_map<ChromeBrowserType, std::string>
    kChromeBrowserTypeToString = {
        {ChromeBrowserType::GoogleChrome, "chrome"},
        {ChromeBrowserType::Brave, "brave"},
        {ChromeBrowserType::Chromium, "chromium"},
        {ChromeBrowserType::Yandex, "yandex"},
        {ChromeBrowserType::Opera, "opera"},
        {ChromeBrowserType::Edge, "edge"},
        {ChromeBrowserType::Edge, "edge_beta"},
};

/// A extension property that needs to be copied
struct ExtensionProperty final {
  enum class Type {
    String,
    StringArray,
  };

  Type type{Type::String};
  std::string path;
  std::string name;
};

/// A list of extension properties that need to be copied
using ExtensionPropertyMap = std::vector<ExtensionProperty>;

/// Active extension properties
const ExtensionPropertyMap kExtensionPropertyList = {
    {ExtensionProperty::Type::String, "name", "name"},
    {ExtensionProperty::Type::String, "update_url", "update_url"},
    {ExtensionProperty::Type::String, "version", "version"},
    {ExtensionProperty::Type::String, "author", "author"},
    {ExtensionProperty::Type::String, "default_locale", "default_locale"},
    {ExtensionProperty::Type::String, "current_locale", "current_locale"},
    {ExtensionProperty::Type::String, "background.persistent", "persistent"},
    {ExtensionProperty::Type::String, "description", "description"},
    {ExtensionProperty::Type::StringArray, "permissions", "permissions"},

    {ExtensionProperty::Type::StringArray,
     "optional_permissions",
     "optional_permissions"},
};

/// Active extension profile settings
const std::vector<std::string> kExtensionProfileSettingsList = {
    "from_webstore", "state", "install_time"};

/// A single profile and browser type
struct ChromeProfilePath final {
  ChromeBrowserType type{ChromeBrowserType::GoogleChrome};
  std::string value;
  std::int64_t uid{};
};

/// A list of profile paths and their types
using ChromeProfilePathList = std::vector<ChromeProfilePath>;

/// Returns a list of possible profile path suffixes
const ChromePathSuffixMap& getChromePathSuffixMap() {
  if (isPlatform(PlatformType::TYPE_WINDOWS)) {
    return kWindowsPathList;

  } else if (isPlatform(PlatformType::TYPE_OSX)) {
    return kMacOsPathList;

  } else {
    return kLinuxPathList;
  }
}

/// Returns an iptree/ptree object from the given JSON string
bool parseJsonString(pt::iptree& tree, const std::string& json) {
  tree = {};

  try {
    std::stringstream json_stream;
    json_stream << json;

    pt::read_json(json_stream, tree);
    return true;

  } catch (const pt::json_parser::json_parser_error&) {
    return false;
  }
}

/// A user id/user home path pair
struct UserInformation final {
  std::int64_t uid{};
  std::string path;
};

/// Returns a list of UserInformation structures matching the given
/// SQL constraints
std::vector<UserInformation> getUserInformationList(
    const QueryContext& context) {
  std::vector<UserInformation> user_info_list;

  auto user_list_as_rows = usersFromContext(context);
  for (const auto& row : user_list_as_rows) {
    if (row.count("uid") == 0 || row.count("directory") == 0) {
      continue;
    }

    const auto& uid_as_string = row.at("uid");
    auto const uid_exp = tryTo<int64_t>(uid_as_string, 10);
    if (uid_exp.isError()) {
      LOG(ERROR) << "Invalid uid field returned: " << uid_as_string;
      continue;
    }

    const auto& path = row.at("directory");

    UserInformation user_info = {};
    user_info.uid = uid_exp.get();
    user_info.path = path;

    user_info_list.push_back(std::move(user_info));
  }

  return user_info_list;
}

/// Returns true if the given path contains either the Preferences
/// or the Secure Preferences file
bool isValidChromeProfile(const fs::path& path) {
  for (const auto& config_file_name_ref : kPossibleConfigFileNames) {
    auto preferences_file_path = path / config_file_name_ref.get();

    auto status = isReadable(preferences_file_path.string());
    if (status.ok()) {
      return true;
    }
  }

  return false;
}

/// Returns a list of chrome profiles matching the given contraints
ChromeProfilePathList getChromeProfilePathList(const QueryContext& context) {
  auto user_info_list = getUserInformationList(context);

  ChromeProfilePathList output;

  for (const auto& user_info : user_info_list) {
    ChromeProfilePath chrome_profile = {};
    chrome_profile.uid = user_info.uid;

    for (const auto& chrome_path_tuple : getChromePathSuffixMap()) {
      const auto& browser_type = std::get<0>(chrome_path_tuple);
      const auto& path_suffix = std::get<1>(chrome_path_tuple);

      chrome_profile.type = browser_type;

      auto path = fs::path(user_info.path) / path_suffix;

      boost::system::error_code error_code;
      auto absolute_chrome_path = fs::canonical(path, error_code);
      if (error_code) {
        absolute_chrome_path = path;
      }

      absolute_chrome_path.make_preferred();

      // Attempt to use the folder as a profile first
      if (isValidChromeProfile(absolute_chrome_path)) {
        chrome_profile.value = absolute_chrome_path.string();
        output.push_back(chrome_profile);

        continue;
      }

      // Attempt to find profiles inside the subdirectories
      std::vector<std::string> chrome_subfolder_list;
      auto status = listDirectoriesInDirectory(absolute_chrome_path.string(),
                                               chrome_subfolder_list);
      if (!status.ok()) {
        continue;
      }

      for (const auto& chrome_subfolder : chrome_subfolder_list) {
        auto subfolder = fs::canonical(chrome_subfolder, error_code);
        if (error_code) {
          subfolder = chrome_subfolder;
        }

        subfolder.make_preferred();

        if (isValidChromeProfile(subfolder)) {
          chrome_profile.value = subfolder.string();
          output.push_back(chrome_profile);

          continue;
        }
      }
    }
  }

  return output;
}

/// Retrieves the list of referenced extensions from the
/// given profile preferences
bool getExtensionPathListFromPreferences(std::vector<std::string>& path_list,
                                         const std::string& profile_path,
                                         const std::string& preferences) {
  path_list = {};

  pt::iptree tree;
  if (!parseJsonString(tree, preferences)) {
    return false;
  }

  const auto& opt_extensions_node = tree.get_child_optional("extensions");

  if (!opt_extensions_node) {
    return true;
  }

  const auto& extensions_node = opt_extensions_node.get();

  auto opt_settings_node = extensions_node.get_child_optional("settings");
  if (!opt_settings_node) {
    opt_settings_node = extensions_node.get_child_optional("opsettings");
  }

  if (!opt_settings_node) {
    return true;
  }

  const auto& settings_node = opt_settings_node.get();

  for (const auto& p : settings_node) {
    const auto& entry_node = p.second;

    const auto& opt_path = entry_node.get_optional<std::string>("path");
    if (!opt_path) {
      continue;
    }

    auto absolute_path = fs::path(opt_path.get());
    if (!absolute_path.is_absolute()) {
      absolute_path = fs::path(profile_path) / "Extensions" / opt_path.get();
    }

    boost::system::error_code error_code;
    auto canonical_path = fs::canonical(absolute_path, error_code);
    if (error_code) {
      canonical_path = absolute_path;
    }

    canonical_path.make_preferred();
    path_list.push_back(canonical_path.string());
  }

  return true;
}

/// Retrieves a list of profiles and extensions with as few parsing
/// as possible
ChromeProfileSnapshotList getChromeProfileSnapshotList(
    const QueryContext& context) {
  ChromeProfileSnapshotList output;

  // Go through each profile we have found thanks to the constraints
  for (const auto& profile_path : getChromeProfilePathList(context)) {
    // Save path and type, so we can add all the chrome-based
    // extensions in the same table
    ChromeProfileSnapshot snapshot = {};
    snapshot.type = profile_path.type;
    snapshot.path = profile_path.value;
    snapshot.uid = profile_path.uid;

    // Save the contents of the configuration files
    auto preferences_file_path =
        fs::path(profile_path.value) / kProfilePreferencesFile;
    auto secure_prefs_file_path =
        fs::path(profile_path.value) / kSecureProfilePreferencesFile;

    /// TODO(alessandro): Remove usage of forensicReadFile()
    auto status = forensicReadFile(secure_prefs_file_path.string(),
                                   snapshot.secure_preferences);

    static_cast<void>(status);

    status =
        forensicReadFile(preferences_file_path.string(), snapshot.preferences);

    static_cast<void>(status);

    if (snapshot.preferences.empty() && snapshot.secure_preferences.empty()) {
      LOG(ERROR) << "Failed to read the Preferences file for the following "
                    "profile snapshot " +
                        preferences_file_path.string();

      continue;
    }

    // Enumerate all the extensions that are present inside this
    // profile. Note that they may not be present in the config
    // file. For now, let's store them all as unreferenced
    auto extensions_folder_path = fs::path(profile_path.value) / "Extensions";

    std::vector<std::string> extension_path_list = {};

    {
      std::vector<std::string> base_path_list;
      auto status = listDirectoriesInDirectory(extensions_folder_path.string(),
                                               base_path_list);

      static_cast<void>(status);

      for (const auto& base_path : base_path_list) {
        std::vector<std::string> new_path_list = {};
        status = listDirectoriesInDirectory(base_path, new_path_list);
        static_cast<void>(status);

        for (auto& new_path : new_path_list) {
          boost::system::error_code error_code;
          auto canonical_path = fs::canonical(new_path, error_code);
          if (!error_code) {
            canonical_path = canonical_path.string();
          }

          canonical_path.make_preferred();
          new_path = canonical_path.string();
        }

        extension_path_list.insert(
            extension_path_list.end(),
            std::make_move_iterator(new_path_list.begin()),
            std::make_move_iterator(new_path_list.end()));
      }
    }

    for (const auto& extension_path : extension_path_list) {
      ChromeProfileSnapshot::Extension extension = {};
      extension.path = extension_path;

      /// TODO(alessandro): Remove usage of forensicReadFile()
      auto manifest_path = fs::path(extension_path) / "manifest.json";
      if (!forensicReadFile(manifest_path.string(), extension.manifest).ok()) {
        LOG(ERROR)
            << "Failed to read the following manifest.json file: "
            << manifest_path.string()
            << ". The extension was referenced by the following profile: "
            << profile_path.value;

        continue;
      }

      snapshot.unreferenced_extensions.insert(
          {extension_path, std::move(extension)});
    }

    // Now get a list of all the extensions referenced by the Preferences file.
    std::vector<std::string> referenced_ext_path_list;

    if (!getExtensionPathListFromPreferences(referenced_ext_path_list,
                                             profile_path.value,
                                             snapshot.preferences)) {
      // Assume this profile is broken and skip it
      LOG(ERROR) << "Failed to parse the following profile: "
                 << profile_path.value;
      continue;
    }

    {
      std::vector<std::string> additional_ref_ext_path_list;
      if (!getExtensionPathListFromPreferences(additional_ref_ext_path_list,
                                               profile_path.value,
                                               snapshot.secure_preferences)) {
        // Assume this profile is broken and skip it
        LOG(ERROR) << "Failed to parse the following profile: "
                   << profile_path.value;
        continue;
      }

      referenced_ext_path_list.insert(
          referenced_ext_path_list.end(),
          std::make_move_iterator(additional_ref_ext_path_list.begin()),
          std::make_move_iterator(additional_ref_ext_path_list.end()));
    }

    for (const auto& referenced_ext_path : referenced_ext_path_list) {
      auto extension_it =
          snapshot.unreferenced_extensions.find(referenced_ext_path);

      if (extension_it != snapshot.unreferenced_extensions.end()) {
        // Move this extension to the referenced group
        auto& extension = extension_it->second;

        snapshot.referenced_extensions.insert(
            {referenced_ext_path, std::move(extension)});

        snapshot.unreferenced_extensions.erase(extension_it);

      } else {
        // This extension is outside the profile, so create a new
        // entry
        ChromeProfileSnapshot::Extension extension = {};
        extension.path = referenced_ext_path;

        /// TODO(alessandro): Remove usage of forensicReadFile()
        auto manifest_path = fs::path(referenced_ext_path) / "manifest.json";
        if (!forensicReadFile(manifest_path.string(), extension.manifest)
                 .ok()) {
          VLOG(1) << "Failed to read the manifest.json file for the following "
                     "extension: "
                  << extension.path
                  << ". The extension was referenced by the following profile: "
                  << profile_path.value;

          continue;
        }

        snapshot.referenced_extensions.insert(
            {referenced_ext_path, std::move(extension)});
      }
    }

    output.push_back(std::move(snapshot));
  }

  return output;
}

Status getLocalizationData(pt::iptree& parsed_localization,
                           const std::string& extension_path,
                           const std::string& locale) {
  parsed_localization = {};

  auto messages_file_path =
      fs::path(extension_path) / "_locales" / locale / "messages.json";

  /// TODO(alessandro): Remove usage of forensicReadFile()
  std::string messages_json;
  auto status = forensicReadFile(messages_file_path, messages_json);
  if (!status.ok()) {
    return Status::failure(
        "Failed to read the localization data for the following locale: " +
        locale);
  }

  pt::iptree output;
  if (!parseJsonString(output, messages_json)) {
    return Status::failure(
        "Failed to parse the localization data for the following locale: " +
        locale);
  }

  parsed_localization = std::move(output);
  return Status::success();
}

Status localizeExtensionProperties(ChromeProfile::Extension& extension) {
  std::string locale;

  auto locale_it = extension.properties.find("default_locale");
  if (locale_it == extension.properties.end()) {
    locale_it = extension.properties.find("current_locale");
  }

  if (locale_it == extension.properties.end()) {
    locale = "en";
  } else {
    locale = locale_it->second;
  }

  auto it = std::find_if(
      extension.properties.begin(),
      extension.properties.end(),

      [](const std::pair<std::string, std::string>& entry) -> bool {
        const auto& property_value = entry.second;

        if (property_value.find(kLocalizedMessagePrefix) == 0U) {
          return true;
        }

        return false;
      });

  auto localization_needed = it != extension.properties.end();
  if (!localization_needed) {
    return Status::success();
  }

  pt::iptree parsed_localization;
  auto status =
      getLocalizationData(parsed_localization, extension.path, locale);
  if (!status.ok()) {
    return status;
  }

  for (auto& p : extension.properties) {
    auto& property_value = p.second;

    if (property_value.find(kLocalizedMessagePrefix) != 0U) {
      continue;
    }

    std::string localized_property_value;
    status = getStringLocalization(
        localized_property_value, parsed_localization, property_value);

    if (!status.ok()) {
      LOG(ERROR) << "Failed to localize string '" << property_value
                 << "' in the following extension: " << extension.path;

    } else {
      property_value = std::move(localized_property_value);
    }
  }

  return Status::success();
}

bool getProperty(std::string& value,
                 const ChromeProfile::Extension::Properties& properties,
                 const std::string& property_name) {
  value = {};

  auto property_it = properties.find(property_name);
  if (property_it == properties.end()) {
    return false;
  }

  value = property_it->second;
  return true;
}

} // namespace

const std::string& getChromeBrowserName(const ChromeBrowserType& type) {
  auto name_it = kChromeBrowserTypeToString.find(type);
  if (name_it == kChromeBrowserTypeToString.end()) {
    static const std::string kNullString{};
    return kNullString;
  }

  const auto& name = name_it->second;
  return name;
}

ChromeProfileList getChromeProfilesFromSnapshotList(
    const ChromeProfileSnapshotList& snapshot_list) {
  ChromeProfileList profile_list;

  for (const auto& snapshot : snapshot_list) {
    ChromeProfile profile = {};
    profile.type = snapshot.type;
    profile.path = snapshot.path;
    profile.uid = snapshot.uid;

    // Parse both configuration files
    pt::iptree parsed_preferences;
    if (!snapshot.preferences.empty() &&
        !parseJsonString(parsed_preferences, snapshot.preferences)) {
      LOG(ERROR)
          << "Failed to parse the Preferences file of the following profile: "
          << profile.path;

      continue;
    }

    pt::iptree parsed_secure_preferences;
    if (!snapshot.secure_preferences.empty() &&
        !parseJsonString(parsed_secure_preferences,
                         snapshot.secure_preferences)) {
      LOG(ERROR) << "Failed to parse the Secure Preferences file of the "
                    "following profile: "
                 << profile.path;

      continue;
    }

    // Try to get the profile name; the Opera browser does not have it
    auto status =
        getProfileNameFromPreferences(profile.name, parsed_preferences);

    if (!status.ok()) {
      status = getProfileNameFromPreferences(profile.name,
                                             parsed_secure_preferences);
    }

    if (!status.ok() && profile.type != ChromeBrowserType::Opera) {
      LOG(ERROR) << "Failed to acquire the name of the following profile: "
                 << profile.path;

      continue;
    }

    // Parse all the extensions that are inside the profile folder but are
    // not referenced by the Preferences file
    for (const auto& ext_p : snapshot.unreferenced_extensions) {
      const auto& ext_snapshot = ext_p.second;

      ChromeProfile::Extension extension = {};
      auto status = getExtensionFromSnapshot(extension, ext_snapshot);
      if (!status.ok()) {
        LOG(ERROR) << "Failed to process the following extension: "
                   << ext_snapshot.path;

        continue;
      }

      profile.extension_list.push_back(std::move(extension));
    }

    // Parse the extensions referenced by the Preferences file
    for (const auto& ext_p : snapshot.referenced_extensions) {
      const auto& ext_snapshot = ext_p.second;

      ChromeProfile::Extension extension = {};
      auto status = getExtensionFromSnapshot(extension, ext_snapshot);
      if (!status.ok()) {
        LOG(ERROR) << "Failed to process the following extension: "
                   << ext_snapshot.path;

        continue;
      }

      status = getExtensionProfileSettings(extension.profile_settings,
                                           parsed_preferences,
                                           ext_snapshot.path,
                                           profile.path);

      if (!status.ok()) {
        status = getExtensionProfileSettings(extension.profile_settings,
                                             parsed_secure_preferences,
                                             ext_snapshot.path,
                                             profile.path);
      }

      if (!status.ok()) {
        LOG(ERROR) << "Failed to parse the profile settings for the following "
                      "extension: "
                   << extension.path << ". Error: " << status.getMessage();
      }

      extension.referenced = true;
      profile.extension_list.push_back(std::move(extension));
    }

    profile_list.push_back(std::move(profile));
  }

  return profile_list;
}

Status getExtensionProfileSettings(
    ChromeProfile::Extension::Properties& profile_settings,
    const pt::iptree& parsed_preferences,
    const std::string& extension_path,
    const std::string& profile_path) {
  profile_settings = {};

  auto opt_ext_settings =
      parsed_preferences.get_child_optional("extensions.settings");

  if (!opt_ext_settings) {
    opt_ext_settings =
        parsed_preferences.get_child_optional("extensions.opsettings");
  }

  if (!opt_ext_settings) {
    return Status::failure("Failed to locate the extensions.settings node");
  }

  const auto& ext_settings = opt_ext_settings.get();

  std::string extension_id;
  pt::iptree extension_obj;

  for (const auto& p : ext_settings) {
    const auto& key_name = p.first;
    const auto& obj = p.second;

    const auto& opt_ext_path = obj.get_optional<std::string>("path");
    if (!opt_ext_path) {
      continue;
    }

    auto ext_path = fs::path(opt_ext_path.get());
    if (!ext_path.is_absolute()) {
      ext_path = fs::path(profile_path) / "Extensions" / ext_path;
    }

    boost::system::error_code error_code;
    auto canonical_path = fs::canonical(ext_path, error_code);
    if (error_code) {
      canonical_path = ext_path;
    }

    if (extension_path == canonical_path) {
      extension_id = key_name;
      extension_obj = obj;

      break;
    }
  }

  if (extension_id.empty()) {
    return Status::failure(
        "Failed to locate the following extension in the preferences: " +
        extension_path);
  }

  profile_settings["identifier"] = extension_id;

  for (const auto& property_name : kExtensionProfileSettingsList) {
    const auto& opt_property = extension_obj.get_child_optional(property_name);
    if (!opt_property) {
      continue;
    }

    const auto& property = opt_property.get();

    auto opt_value = property.get_value_optional<std::string>();
    if (!opt_value) {
      continue;
    }

    const auto& value = opt_value.get();
    profile_settings[property_name] = value;
  }

  return Status::success();
}

Status getExtensionFromSnapshot(
    ChromeProfile::Extension& extension,
    const ChromeProfileSnapshot::Extension& snapshot) {
  extension = {};

  ChromeProfile::Extension output;
  output.path = snapshot.path;

  output.manifest_hash = hashFromBuffer(
      HASH_TYPE_SHA256, snapshot.manifest.c_str(), snapshot.manifest.size());

  pt::iptree parsed_manifest;
  if (!parseJsonString(parsed_manifest, snapshot.manifest)) {
    return Status::failure(
        "Failed to parse the Manifest file for the following extension: " +
        snapshot.path);
  }

  auto status = getExtensionProperties(output.properties, parsed_manifest);
  if (!status.ok()) {
    return status;
  }

  status = localizeExtensionProperties(output);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to process the localization settngs for the "
                  "following extension: "
               << output.path;
  }

  output.content_scripts_matches =
      getExtensionContentScriptsMatches(parsed_manifest);

  // Re-render the manifest file to json, this time without
  // unnecessary whitespace
  std::stringstream stream;
  pt::write_json(stream, parsed_manifest, false);
  output.manifest_json = stream.str();

  extension = std::move(output);
  return Status::success();
}

Status getProfileNameFromPreferences(std::string& name,
                                     const pt::iptree& parsed_preferences) {
  name = {};

  const auto& opt_profile_node =
      parsed_preferences.get_child_optional("profile");

  if (!opt_profile_node) {
    return Status::failure("The 'profile' object was not found");
  }

  const auto& profile_node = opt_profile_node.get();

  auto output = profile_node.get<std::string>("name", "");
  if (output.empty()) {
    return Status::failure("The 'name' value was not found");
  }

  name = std::move(output);
  return Status::success();
}

Status getExtensionProperties(ChromeProfile::Extension::Properties& properties,
                              const pt::iptree& parsed_manifest) {
  properties = {};

  for (const auto& property : kExtensionPropertyList) {
    const auto& opt_node = parsed_manifest.get_child_optional(property.path);
    if (!opt_node) {
      continue;
    }

    const auto& node = opt_node.get();

    if (property.type == ExtensionProperty::Type::String) {
      auto opt_property_value = node.get_value_optional<std::string>();
      if (!opt_property_value) {
        continue;
      }

      const auto& property_value = opt_property_value.get();
      properties.insert({property.name, property_value});

    } else if (property.type == ExtensionProperty::Type::StringArray) {
      std::string list_value;

      for (const auto& p : node) {
        const auto& child_node = p.second;

        auto opt_child_node_value =
            child_node.get_value_optional<std::string>();

        if (!opt_child_node_value) {
          continue;
        }

        auto child_node_value = opt_child_node_value.get();

        if (!list_value.empty()) {
          list_value += ", ";
        }

        list_value += child_node_value;
      }

      properties.insert({property.name, list_value});

      // Also provide the json-encoded value
      std::stringstream stream;
      pt::write_json(stream, node, false);

      list_value = stream.str();
      properties.insert({property.name + "_json", std::move(list_value)});

    } else {
      LOG(ERROR) << "Invalid property type specified: "
                 << static_cast<int>(property.type);
      continue;
    }
  }

  return Status::success();
}

ChromeProfileList getChromeProfiles(const QueryContext& context) {
  auto snapshot_list = getChromeProfileSnapshotList(context);
  return getChromeProfilesFromSnapshotList(snapshot_list);
}

Status getStringLocalization(std::string& localized_string,
                             const pt::iptree& parsed_localization,
                             const std::string string) {
  localized_string = string;

  if (string.find(kLocalizedMessagePrefix) != 0U) {
    localized_string = string;
    return Status::success();
  }

  auto string_name_start = kLocalizedMessagePrefix.size();
  auto string_name_end = string.size() - string_name_start - 2U;
  auto string_name = string.substr(string_name_start, string_name_end);

  auto string_node_path = std::string(string_name) + ".message";

  auto opt_string_node =
      parsed_localization.get_child_optional(string_node_path);

  if (!opt_string_node) {
    return Status::failure("No localization found for the following key: " +
                           string);
  }

  const auto& string_node = opt_string_node.get();

  const auto& opt_string = string_node.get_value_optional<std::string>();
  if (!opt_string) {
    return Status::failure(
        "Invalid localization found for the following key: " + string);
  }

  localized_string = opt_string.get();
  return Status::success();
}

ContentScriptsEntryList getExtensionContentScriptsMatches(
    const pt::iptree& parsed_manifest) {
  const auto& opt_content_scripts_node =
      parsed_manifest.get_child_optional("content_scripts");

  if (!opt_content_scripts_node) {
    return {};
  }

  const auto& content_scripts_node = opt_content_scripts_node.get();
  ContentScriptsEntryList entry_list;

  for (const auto& content_scripts_entry : content_scripts_node) {
    const auto& entry_node = content_scripts_entry.second;

    const auto& opt_matches_node = entry_node.get_child_optional("matches");
    if (!opt_matches_node) {
      continue;
    }

    const auto& matches_node = opt_matches_node.get();

    const auto& opt_js_node = entry_node.get_child_optional("js");
    if (!opt_js_node) {
      continue;
    }

    const auto& js_node = opt_js_node.get();

    for (const auto& match_entry : matches_node) {
      const auto& match_value_node = match_entry.second;

      auto opt_match_value = match_value_node.get_value_optional<std::string>();
      if (!opt_match_value) {
        continue;
      }

      const auto& match_value = opt_match_value.get();

      ContentScriptsEntry entry = {};
      entry.match = match_value;

      for (const auto& js_entry : js_node) {
        const auto& js_value_node = js_entry.second;

        auto opt_js_value = js_value_node.get_value_optional<std::string>();
        if (!opt_js_value) {
          continue;
        }

        const auto& js_value = opt_js_value.get();
        entry.script = js_value;

        entry_list.push_back(entry);
      }
    }
  }

  return entry_list;
}

std::string getExtensionProperty(const ChromeProfile::Extension& extension,
                                 const std::string& property_name,
                                 bool optional,
                                 const std::string& default_value) {
  std::string value;
  if (!getProperty(value, extension.properties, property_name)) {
    if (!optional) {
      LOG(ERROR) << "The following extension is missing the '" << property_name
                 << "' property: " << extension.path;
    }

    value = default_value;
  }

  return value;
}

std::string getExtensionProfileSettingsValue(
    const ChromeProfile::Extension& extension,
    const std::string& property_name) {
  std::string value;
  auto succeeded =
      getProperty(value, extension.profile_settings, property_name);
  static_cast<void>(succeeded);

  return value;
}

} // namespace tables

} // namespace osquery
