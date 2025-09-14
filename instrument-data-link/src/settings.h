#pragma once

#include <string>

struct DataLinkSettings {
    std::string host;
    int instrumentListenPort;
    int simulatorListenPort;
    int dataRateFps;
};

struct InstrumentPanelSettings {
    std::string host;
    int listenPort;
};

struct SimulatorDataSettings {
    std::string host;
    int simulatorListenPort;
};

struct AppSettings {
    DataLinkSettings dataLink;
    InstrumentPanelSettings instrumentPanel;
    SimulatorDataSettings simulatorData;
};

class SettingsManager {
public:
    static bool loadSettings(const std::string& filePath, AppSettings& settings);
    static AppSettings getDefaultSettings();
private:
    static bool parseJson(const std::string& jsonContent, AppSettings& settings);
};
