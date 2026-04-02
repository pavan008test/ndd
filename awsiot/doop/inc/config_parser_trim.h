// This file is trimmed version of nd_core_utils/cpp/src/config_parser.cpp, where only minimal functions required to make doop binary standalone are included.
// The TAG used at the time is "6.12"

#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <iostream>
#include "iniparser.h"

#define CONFIG_PARSE_LOCK true
#define CONFIG_PARSE_NO_LOCK false
static const std::string default_override_ini_file = "/home/ubuntu/config/bagheera_override.ini";


class Config_parser
{

private:
	std::string m_fileName;
	int m_numSections;
	bool m_parseStatus;
	bool m_emptyFile;
	const dictionary *m_dictionary;
	const dictionary *m_override_dictionary;
	std::map<std::string,std::string> m_map;
	std::map<std::string,int> m_secDetails;
	void fill_tables();

public:
	Config_parser(const std::string& fileName, bool action = CONFIG_PARSE_NO_LOCK, 
						std::string override_ini_file = default_override_ini_file);
    ~Config_parser();

	bool getParseStatus();

	std::string getConfig (std::string section, std::string key,
				std::string defValue, bool action = CONFIG_PARSE_NO_LOCK);
};

#endif

