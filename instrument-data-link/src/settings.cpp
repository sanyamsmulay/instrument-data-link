#include "settings.h"
#include <fstream>
#include <iostream>
#include <sstream>

// Simple JSON parser for our specific use case
bool SettingsManager::parseJson(const std::string& jsonContent, AppSettings& settings) {
    // Initialize with defaults
    settings = getDefaultSettings();
    
    std::istringstream stream(jsonContent);
    std::string line;
    std::string currentSection;
    
    while (std::getline(stream, line)) {
        // Remove whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        if (line.empty() || line[0] == '{' || line[0] == '}') {
            continue;
        }
        
        // Check for section headers
        if (line.find("\"Data Link\"") != std::string::npos) {
            currentSection = "DataLink";
            continue;
        } else if (line.find("\"Instrument Panel\"") != std::string::npos) {
            currentSection = "InstrumentPanel";
            continue;
        } else if (line.find("\"Simulator Data\"") != std::string::npos) {
            currentSection = "SimulatorData";
            continue;
        }
        
        // Parse key-value pairs
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;
        
        std::string key = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 1);
        
        // Clean up key and value
        key.erase(0, key.find_first_not_of(" \t\""));
        key.erase(key.find_last_not_of(" \t\"") + 1);
        value.erase(0, value.find_first_not_of(" \t\""));
        value.erase(value.find_last_not_of(" \t\",") + 1);
        
        // Parse values based on current section
        if (currentSection == "DataLink") {
            if (key == "Host") {
                settings.dataLink.host = value;
            } else if (key == "Instrument Listen Port") {
                settings.dataLink.instrumentListenPort = std::stoi(value);
            } else if (key == "Simulator Listen Port") {
                settings.dataLink.simulatorListenPort = std::stoi(value);
            } else if (key == "Data Rate FPS") {
                settings.dataLink.dataRateFps = std::stoi(value);
            }
        } else if (currentSection == "InstrumentPanel") {
            if (key == "Host") {
                settings.instrumentPanel.host = value;
            } else if (key == "Listen Port") {
                settings.instrumentPanel.listenPort = std::stoi(value);
            }
        } else if (currentSection == "SimulatorData") {
            if (key == "Host") {
                settings.simulatorData.host = value;
            } else if (key == "Simulator Listen Port") {
                settings.simulatorData.simulatorListenPort = std::stoi(value);
            }
        }
    }
    
    return true;
}

bool SettingsManager::loadSettings(const std::string& filePath, AppSettings& settings) {
    // Use synchronous file operations
    FILE* fp = fopen(filePath.c_str(), "rb");
    if (!fp) {
        std::cerr << "Warning: Could not open settings file: " << filePath << std::endl;
        std::cerr << "Using default settings." << std::endl;
        settings = getDefaultSettings();
        return false;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // Read entire file content
    std::string content;
    content.resize(fileSize);
    size_t bytesRead = fread(&content[0], 1, fileSize, fp);
    fclose(fp);
    
    if (bytesRead != static_cast<size_t>(fileSize)) {
        std::cerr << "Error: Failed to read settings file" << std::endl;
        settings = getDefaultSettings();
        return false;
    }
    
    try {
        bool result = parseJson(content, settings);
        if (result) {
            printf("Settings loaded from: %s\n", filePath.c_str());
            printf("Configuration:\n");
            printf("  Data Link:\n");
            printf("    Host: %s\n", settings.dataLink.host.c_str());
            printf("    Instrument Listen Port: %d\n", settings.dataLink.instrumentListenPort);
            printf("    Simulator Listen Port: %d\n", settings.dataLink.simulatorListenPort);
            printf("  Instrument Panel:\n");
            printf("    Host: %s\n", settings.instrumentPanel.host.c_str());
            printf("    Listen Port: %d\n", settings.instrumentPanel.listenPort);
            printf("  Simulator Data:\n");
            printf("    Host: %s\n", settings.simulatorData.host.c_str());
            printf("    Simulator Listen Port: %d\n", settings.simulatorData.simulatorListenPort);
        }
        return result;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing settings file: " << e.what() << std::endl;
        std::cerr << "Using default settings." << std::endl;
        settings = getDefaultSettings();
        return false;
    }
}

AppSettings SettingsManager::getDefaultSettings() {
    AppSettings settings;
    
    // Data Link defaults
    settings.dataLink.host = "127.0.0.1"; // default to localhost
    settings.dataLink.instrumentListenPort = 52021;
    settings.dataLink.simulatorListenPort = 52022;
    settings.dataLink.dataRateFps = 10;
    
    // Instrument Panel defaults
    settings.instrumentPanel.host = "127.0.0.1"; // default to localhost
    settings.instrumentPanel.listenPort = 52021;
    
    // Simulator Data defaults
    settings.simulatorData.host = "127.0.0.1";
    settings.simulatorData.simulatorListenPort = 52023;
    
    return settings;
}
