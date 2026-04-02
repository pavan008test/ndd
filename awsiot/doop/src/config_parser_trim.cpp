/*
 * config_parser_trim.cpp
 * 
 * Standalone implementation of config parser functions for doop binary
 * This file contains copied Config_parser class methods from nd_core_utils/cpp/src/config_parser.cpp
 * to make doop binary standalone as much as possible.
 */

#include "config_parser_trim.h"
#define TAG "CST_CFG_PRSR"
#define FLOCK_RETRY_COUNT 5
#include <sys/file.h>
using namespace std;

// File locking type enumeration - copied from nd_core_utils/cpp/src/config_parser.cpp (line 10)
enum elock_type
{
    eLOCK_READ = 0,
    eLOCK_WRITE
};

/*
 * fill_tables - Populate internal maps with sections and key-value pairs from dictionary
 * Copied from: nd_core_utils/cpp/src/config_parser.cpp (line 16)
 */
void Config_parser::fill_tables()
{
	m_secDetails.clear();
	m_map.clear();

	int numKeys = 0; // num of keys in one sections.
	vector<char *> keys; //holds key names

	m_numSections = iniparser_getnsec (m_dictionary);
	// Capture names of all sections in a vector for future use.
	for (int i=0; i<m_numSections; i++)
	{
		const char *secName = iniparser_getsecname(m_dictionary, i);
		m_secDetails.insert(make_pair(secName,0));
	}

	map<string,int>::iterator it = m_secDetails.begin();
	string def("");

	while (it != m_secDetails.end())
	{
		numKeys = iniparser_getsecnkeys (m_dictionary, (it->first).c_str());
		it->second = numKeys;
		keys.resize(numKeys);
		iniparser_getseckeys(m_dictionary, (it->first).c_str(), (const char**)&keys[0]);
		for (int i =0; i<numKeys;i++)
		{
			const char *val = iniparser_getstring (m_dictionary, keys[i], def.c_str());
			m_map.insert(make_pair(keys[i],val));
		}
		++it;
	}
}


/*
 * open_and_lock - Open file and acquire file lock for safe concurrent access
 * Copied from: nd_core_utils/cpp/src/config_parser.cpp (line 66)
 * Simplified version using cerr instead of LOG_E/LOG_D macros
 */
static bool open_and_lock (string filename, FILE **fpp, bool need_lock, elock_type lock_type = eLOCK_WRITE)
{
    bool ret = false;
    bool locked = true;

    if (filename.empty())
    {
        cerr << TAG << " Filename is empty" << endl;
        ret = false;
    }
    else if (fpp == NULL)
    {
        cerr << TAG << " fpp is NULL" << endl;
        ret = false;
    }
    else
    {
        *fpp = fopen(filename.c_str(),"r+");
        if (!(*fpp))
        {
            cerr << TAG << " Failed to open " << filename << endl;
            ret = false;
        }
        else if (need_lock)
        {
            int fd = fileno (*fpp);
            if (fd < 0)
            {
                cerr << TAG << " fileno returned negative value" << endl;
                ret = false;
            }
            else
            {
                int count = 0;
                int operation = (lock_type == eLOCK_WRITE) ? (LOCK_EX | LOCK_NB) : (LOCK_SH | LOCK_NB);

                cerr << TAG << " Trying to lock file " << filename << endl;
                while (flock (fd, operation) != 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    else if (errno == EWOULDBLOCK)
                    {
                        count++;
                        if (count == FLOCK_RETRY_COUNT)
                        {
                            cerr << TAG << " Couldn't lock file after " << FLOCK_RETRY_COUNT << " trials" << endl;
                            locked = false;
                            break;
                        }
                        else
                        {
                            sleep(1);
                            continue;
                        }
                    }
                    else
                    {
                        int err = errno;
                        cerr << TAG << " flock failed with errno : " << err << endl;
                        locked = false;
                        break;
                    }
                }
                if (locked)
                {
                    cerr << TAG << " Locked file " << filename << endl;
                }
                else
                {
                    cerr << TAG << " Failed to lock file " << filename << endl;
                }
                ret = locked;
            }
        }
        else
        {
            cerr << TAG << " Locking not needed" << endl;
            ret =true;
        }
    }
    return ret;
}

/*
 * unlock_and_close - Release file lock and close file
 * Copied from: nd_core_utils/cpp/src/config_parser.cpp (line 161)
 * Simplified version using cerr instead of LOG_E/LOG_D macros
 */
static bool unlock_and_close (FILE *fp, bool need_lock)
{
    bool ret = false;
    bool unlocked = true;

    if (!fp)
    {
        return true;
    }

    if (need_lock)
    {
        int fd = fileno (fp);
        if (fd < 0)
        {
            cerr << TAG << " fileno returned negative value" << endl;
            ret = false;
        }
        else
        {
            int count = 0;
            cerr << TAG << " Trying to unlock file" << endl;
            while (flock (fd, LOCK_UN | LOCK_NB) != 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                else if (errno == EWOULDBLOCK)
                {
                    count++;
                    if (count == FLOCK_RETRY_COUNT)
                    {
                        cerr << TAG << " Couldn't unlock file after " << FLOCK_RETRY_COUNT << " trials" << endl;
                        unlocked = false;
                        break;
                    }
                    else
                    {
                        sleep(1);
                        continue;
                    }
                }
                else
                {
                    int err = errno;
                    cerr << TAG << " flock-unlock failed with errno:" << err << endl;
                    unlocked = false;
                    break;

                }
            }
            if (unlocked)
            {
                cerr << TAG << " Unlocked file" << endl;
            }
            else
            {
                cerr << TAG << " Failed unlocking file" << endl;
            }
            ret = unlocked;
        }

    }
    else
    {
        cerr << TAG << " File was not locked. Just closing" << endl;
        ret =true;
    }
    if (fp)
    {
        fclose (fp);
    }
    return ret;
}

/*
 * is_file_empty - Check if file pointer points to an empty file
 * Copied from: nd_core_utils/cpp/src/config_parser.cpp (line 237)
 * Simplified version using cerr instead of LOG_E macros
 */
static bool is_file_empty(FILE *fp)
{
    bool empty = false;
    if (fseek (fp, 0 , SEEK_END) == 0)
    {
        long fsize = ftell (fp);
        if (fsize == 0)
        {
            cerr << TAG << " ini file is empty" << endl;
            empty = true;
        }
        else if (fseek (fp, 0, SEEK_SET))
        {
            cerr << TAG << " Failed to seek back to beginning of ini file" << endl;
            empty = true;
        }
    }
    else
    {
        cerr << TAG << " Seeking ini file failed" << endl;
        empty = true;
    }
    return empty;
}

/*
 * parse_ini_file - Parse INI file using iniparser library
 * Copied from: nd_core_utils/cpp/src/config_parser.cpp (line 259)
 */
static const dictionary *parse_ini_file (string filename, FILE *fp)
{
    const dictionary *dict = NULL;
    dict = iniparser_load (filename.c_str(), fp);
    return dict;
}

/*
 * Config_parser - Constructor to initialize config parser with INI file
 * Copied from: nd_core_utils/cpp/src/config_parser.cpp (line 269)
 * Simplified version using cout/cerr instead of LOG_I/LOG_E macros
 */
Config_parser::Config_parser(const std::string& fileName, bool action, 
                            std::string override_ini_file)
{
    m_fileName.clear();
    m_numSections = 0;
    m_parseStatus = false;
    m_dictionary = NULL;
    m_override_dictionary = NULL;
    m_map.clear();
    m_secDetails.clear();
    FILE *fp = NULL;

    if (open_and_lock (fileName, &fp, action, eLOCK_READ))
    {

        m_fileName = fileName;
        if (!is_file_empty(fp))
        {
            m_dictionary = parse_ini_file(fileName, fp);
            if (!m_dictionary)
            {
                m_parseStatus = false;
            }
            else
            {
                m_parseStatus = true;
                fill_tables();
            }

        }
        else
        {
            m_emptyFile = true;
        }
    }
    unlock_and_close (fp, action);

    FILE *fp_override = NULL;
    if (open_and_lock (override_ini_file, &fp_override, true, eLOCK_READ))
    {
        cout << TAG << " Override file " << override_ini_file << " present" << endl;
        m_override_dictionary = parse_ini_file (override_ini_file, fp_override);
        if (!m_override_dictionary)
        {
            cerr << TAG << " Override file parsing failed" << endl;
            unlock_and_close (fp_override, action);
            return;
        }
        cout << TAG << " Override file parsed successfully" << endl;
    }
    else
    {
        cerr << TAG << " open_and_lock failed for " << override_ini_file << endl;
    }
    unlock_and_close (fp_override, action);
}

/*
 * getParseStatus - Return parsing status of the configuration file
 * Copied from: nd_core_utils/cpp/src/config_parser.cpp (line 326)
 */
bool Config_parser::getParseStatus()
{
	return m_parseStatus;
}

/*
 * getConfig - Retrieve configuration value for given section and key
 * Copied from: nd_core_utils/cpp/src/config_parser.cpp (line 709)
 * Simplified version using cerr instead of LOG_E macros
 */
string Config_parser::getConfig (string section, string key, string defValue, bool need_lock)
{

    if (!m_parseStatus || section.empty() || key.empty())
    {
        cerr << TAG << " getConfig: m_parseStatus = " << m_parseStatus << " section = " << section << " key=" << key << endl;
        return defValue;
    }

	transform (section.begin(), section.end(), section.begin(), ::tolower);
	transform (key.begin(), key.end(), key.begin(), ::tolower);

    FILE *fp = NULL;
    const dictionary *dict = NULL;

    if (!open_and_lock (m_fileName, &fp, need_lock, eLOCK_READ))
    {
        cerr << TAG << " getConfig: open_and_lock failed" << endl;
        unlock_and_close(fp, need_lock);
        return defValue;
    }
    else
    {
        dict = parse_ini_file (m_fileName, fp);
        if (!dict)
        {
            cerr << TAG << " getConfig: parsing failed" << endl;
            unlock_and_close (fp, need_lock);
            return defValue;
        }
        else
        {
            //If parsing is success, free current dictionary and assign this new
            //dictionary to m_dictionary
            iniparser_freedict ((dictionary *)m_dictionary);
            m_dictionary = dict;
            fill_tables();
        }
        map<string,string>::const_iterator search = m_map.find(section+":"+key);
        if (search != m_map.end())
        {
            unlock_and_close(fp, need_lock);
            return search->second;
        }
        else
        {
            cerr << TAG << " getConfig - " << section << ":" << key << " not found" << endl;
            unlock_and_close(fp, need_lock);
            return defValue;
        }
    }
}

/*
 * ~Config_parser - Destructor to clean up allocated dictionaries
 * Copied from: nd_core_utils/cpp/src/config_parser.cpp (line 919)
 */
Config_parser::~Config_parser()
{
    if (m_dictionary)
    {
        iniparser_freedict ((dictionary *)m_dictionary);
    }
    m_dictionary = NULL;

    if (m_override_dictionary)
    {
        iniparser_freedict ((dictionary *) m_override_dictionary);
    }
    m_override_dictionary = NULL;
}
