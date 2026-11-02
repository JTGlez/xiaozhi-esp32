/* Host stub for settings.h (NVS-backed in firmware). Values persist in a
 * process map so theme save/load side effects remain observable. */
#ifndef HOST_SETTINGS_H
#define HOST_SETTINGS_H

#include <string>

class Settings {
public:
    Settings(const std::string& ns, bool read_only);
    std::string GetString(const std::string& key, const std::string& default_value);
    void SetString(const std::string& key, const std::string& value);
};

/* Host control: read a persisted value ("display"/"theme" in production). */
const char* host_settings_get(const char* ns, const char* key);

#endif /* HOST_SETTINGS_H */
