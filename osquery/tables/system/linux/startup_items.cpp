/**
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed in accordance with the terms specified in
 *  the LICENSE file found in the root directory of this source tree.
 */

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include <string.h>

#include <systemd/sd-bus.h>

#include <osquery/core.h>
#include <osquery/filesystem/filesystem.h>
#include <osquery/logger.h>
#include <osquery/tables.h>
#include <osquery/utils/conversions/split.h>

namespace fs = boost::filesystem;

namespace osquery {
namespace tables {

const std::vector<std::string> systemItemPaths = {"/etc/xdg/autostart/"};

const std::vector<std::string> systemScriptPaths = {"/etc/init.d/"};

void genAutoStartItems(const std::string& sysdir, QueryData& results) {
  try {
    std::vector<std::string> dirFiles;
    osquery::listFilesInDirectory(sysdir, dirFiles, false);
    for (const auto& file : dirFiles) {
      Row r;
      std::string content;
      if (readFile(file, content)) {
        for (const auto& line : osquery::split(content, "\n")) {
          if (line.find("Name=") == 0) {
            auto details = osquery::split(line, "=");
            if (details.size() == 2) {
              r["name"] = details[1];
            }
          }
          if (line.find("Exec=") == 0) {
            auto details = osquery::split(line, "=");
            if (details.size() == 2) {
              r["path"] = details[1];
            }
          }
        }
      }
      r["type"] = "Startup Item";
      r["status"] = "enabled";
      r["source"] = sysdir;

      auto username = osquery::split(sysdir, "/");
      if (username.size() > 1 && username[0] == "home") {
        r["username"] = username[1];
      }
      results.push_back(r);
    }
  } catch (const Status e) {
    VLOG(1) << "Error traversing " << sysdir << ": " << e.what();
  }
}

void genAutoStartScripts(const std::string& sysdir, QueryData& results) {
  try {
    std::vector<std::string> dirFiles;
    osquery::listFilesInDirectory(sysdir, dirFiles, false);
    for (const auto& file : dirFiles) {
      Row r;
      r["name"] = osquery::split(file, "/").back();
      r["path"] = file;
      r["type"] = "Startup Item";
      r["status"] = "enabled";
      r["source"] = sysdir;
      auto username = osquery::split(sysdir, "/");
      if (username.size() > 1 && username[0] == "home") {
        r["username"] = username[1];
      }
      results.push_back(r);
    }
  } catch (const Status e) {
    VLOG(1) << "Error traversing " << sysdir << ": " << e.what();
  }
}

void genSystemDItems(QueryData& results) {
        sd_bus_message *reply = NULL;
        sd_bus_message *m = NULL;
        sd_bus *bus;
        char *state = NULL;
        char *path = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r = 0;

        int s = sd_bus_open_system(&bus);
        if (s < 0) {
                return;
        }

        s = sd_bus_message_new_method_call(bus,
                                     &m,
                                     "org.freedesktop.systemd1",
                                     "/org/freedesktop/systemd1",
                                     "org.freedesktop.systemd1.Manager",
                                     "ListUnitFiles");
        if (s < 0) {
                VLOG(1) << "Bus error " << strerror(r);
                return;
        }
        s = sd_bus_call(bus, m, 0, &error, &reply);
        if (s < 0) {
                VLOG(1) << "Bus error " << strerror(r);
                return;
        }
        while ((s = sd_bus_message_read(reply, "(ss)", &path, &state)) > 0) {
                Row r;
                // name
                // path
                r["type"] = "Startup Item";
                r["status"] = std::string(state); //possibly change
                r["source"] = std::string(path);
                results.push_back(r);
        }
        s = sd_bus_message_exit_container(reply);
        if (s < 0) {
                VLOG(1) << "Bus error " << strerror(r);
                return;
        }
}

QueryData genStartupItems(QueryContext& context) {
  QueryData results;

  // User specific
  for (const auto& dir : getHomeDirectories()) {
    auto itemsDir = dir / "/.config/autostart/";
    auto scriptsDir = dir / "/.config/autostart-scripts/";
    genAutoStartItems(itemsDir.string(), results);
    genAutoStartScripts(scriptsDir.string(), results);
  }

  // System wide
  for (const auto& dir : systemScriptPaths) {
    genAutoStartScripts(dir, results);
  }
  for (const auto& dir : systemItemPaths) {
    genAutoStartItems(dir, results);
  }
  genSystemDItems(results);

  return results;
}

} // namespace tables
} // namespace osquery
