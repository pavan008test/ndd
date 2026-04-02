/*
doop - stands for do operation, it can be operate or reoperate.
Name is intentionally kept bland for security reasons.
*/

#include <iostream>
#include <string>
#include <cstdlib>
#include <doop_patch.h>
#include <nd_auth_openssl.h>
#include <config_parser_trim.h>

using namespace std;

#ifdef BAGHEERA2
    static string dev_shm_dir = "/dev/shm/nd_files_c/";
#elif KRAIT
    static string dev_shm_dir = "/dev/shm/";
#endif

bool is_operate = false;

static const string DOOPEY = "R$PO~UN@%R@D)*&E";

static const string nddevice_ini = "/home/ubuntu/.nddevice/nddevice.ini";

static string jwt_encoded_tmp_path = dev_shm_dir + "ekod"; // File name is dynamically set using md5sum
static const string jwt_operated_tmp_path = dev_shm_dir + "ekpt.txt";
static const string jwt_cert_path = "/home/ubuntu/.nddevice/certificate/ed25519key.pem";

static string iot_encoded_tmp_path = dev_shm_dir + "ptod"; // File name is dynamically set using md5sum
static const string iot_operated_tmp_path = dev_shm_dir + "ptpt.txt";
static const string iot_cert_path = "/home/ubuntu/.nddevice/certificate/private.pem.key";

static string jwt_bkp_path = "";
static string jwt_bkp_encoded_tmp_path = dev_shm_dir + "bkod"; // File name is dynamically set using md5sum
static const string jwt_bkp_operated_tmp_path = dev_shm_dir + "bkpt.txt"; // Backup file for JWT encrypted data

string xOR(const string& data, const string& key) {
    string encodedData = data;
    for (size_t index = 0; index < data.size(); ++index) {
        encodedData[index] = data[index] ^ key[index % key.size()]; // XOR with repeating key
    }
    return encodedData;
}

// Encode the buffer to a file using XOR
bool encode_buf_to_file(const unsigned char* buffer, size_t len, const string& out_path) {
    bool result = false;
    do {
        string data(reinterpret_cast<const char*>(buffer), len);
        if (data.empty()) {
            cerr << "encode_buf_to_file: Data is empty after conversion." << endl;
            break;
        }

        string encoded = xOR(data, DOOPEY);
        FILE* fp = fopen(out_path.c_str(), "wb");
        if (!fp) {
            cerr << "Failed to open output file for writing: " << out_path << endl;
            break;
        }

        size_t written = fwrite(encoded.data(), 1, encoded.size(), fp);
        fclose(fp);

        if (written != encoded.size()) {
            cerr << "Failed to write all encoded data to output file: " << out_path << endl;
            break;
        }

        result = true;
    } while (false);

    return result;
}

// Decode the file to a buffer using XOR
bool decode_file_to_buffer(const string& input_path, unsigned char*& buffer, size_t& buffer_len) {
    bool result = false;
    FILE* fp = nullptr;

    do {
        fp = fopen(input_path.c_str(), "rb");
        if (!fp) {
            cerr << "decode_file_to_buffer: Failed to open input file: " << input_path << endl;
            break;
        }

        if (fseek(fp, 0, SEEK_END) != 0) {
            cerr << "decode_file_to_buffer: Failed to seek end of input file: " << input_path << endl;
            break;
        }

        long fsize = ftell(fp);
        if (fsize <= 0) {
            cerr << "decode_file_to_buffer: Input file is empty or error: " << input_path << endl;
            break;
        }
        rewind(fp);

        string encoded(static_cast<size_t>(fsize), '\0');
        size_t read = fread(&encoded[0], 1, static_cast<size_t>(fsize), fp);
        if (read != static_cast<size_t>(fsize)) {
            cerr << "decode_file_to_buffer: Failed to read all data from input file: " << input_path << endl;
            break;
        }

        string decoded = xOR(encoded, DOOPEY);

        // Allocate buffer and copy decoded data
        buffer_len = decoded.size();
        buffer = static_cast<unsigned char*>(malloc(buffer_len));
        if (!buffer) {
            cerr << "decode_file_to_buffer: Failed to allocate memory for buffer" << endl;
            buffer_len = 0;
            break;
        }

        memcpy(buffer, decoded.data(), buffer_len);
        result = true;

    } while (false);

    // Cleanup
    if (fp) {
        fclose(fp);
        fp = nullptr;
    }
    
    // If operation failed, cleanup buffer
    if (!result && buffer) {
        free(buffer);
        buffer = nullptr;
        buffer_len = 0;
    }

    return result;
}

// Validate and parse command line arguments
bool parse_args(int argc, char* argv[]) {
    if (argc != 2) {
        return false;
    }
    
    int operation = atoi(argv[1]);
    if (operation != 0 && operation != 1) {
        cerr << "Invalid operation mode: " << operation << endl;
        return false;
    }
    
    is_operate = (operation == 0)? true : false; // 0 for operate, 1 for reoperate

    return true;
}

// Check the OTA upgrade state from the nddevice.ini file
bool check_ota_upgrade_state() {
    Config_parser c(nddevice_ini);
    if (c.getParseStatus() != true)
    {
        cout << "Error in parsing nddevice config file" << endl;
        return false;
    }
   
    string upgrade_state = c.getConfig("upgrade", "state", "");
    if (upgrade_state != "UPDATE_AVAILABLE")
    {
        return false;
    }
    
    return true; 
}

bool write_buf_to_file(const unsigned char* buffer, size_t buffer_len, const string& file_path) {
    bool result = false;
    FILE* fp = nullptr;

    do {
        if (!buffer || buffer_len == 0) {
            cerr << "write_buf_to_file: Invalid buffer or buffer length" << endl;
            break;
        }

        fp = fopen(file_path.c_str(), "wb");
        if (!fp) {
            cerr << "write_buf_to_file: Failed to open file for writing: " << file_path << endl;
            break;
        }

        size_t written = fwrite(buffer, 1, buffer_len, fp);
        if (written != buffer_len) {
            cerr << "write_buf_to_file: Failed to write all data to file: " << file_path << endl;
            break;
        }

        result = true;

    } while (false);

    // Cleanup
    if (fp) {
        fclose(fp);
        fp = nullptr;
    }

    return result;
}

// int counter = 0; 
bool reoperate_encode_and_verify(const string& src_path, string& dest_path, vector<int>&operations_status) {
    bool overall_status = false;

    do{
        // Step 0: Check if source file exists
        if (false == file_is_present(src_path)) {
            cerr << "Source file does not exist for reoperate_encode_and_verify: " << src_path << endl; 
            break;
        }

        // Step 1: reoperate src_path to a buffer and note md5sum of the reoperated buffer
        unsigned char* dest_buffer = nullptr;
        size_t dest_len = nd_file_reoperate_to_buffer(src_path.c_str(), &dest_buffer);

        if(dest_len <=0 || dest_buffer == nullptr) {
            cerr << "reoperate failed for source file: " << src_path << endl;
            operations_status[3] = 0; // Mark operation as failed
            break;
        }

        string md5sum_of_reoperated_buffer;
        calculate_md5sum(dest_buffer, dest_len, md5sum_of_reoperated_buffer);// API always returns true, so no need to check return value
        dest_path += ("_" + md5sum_of_reoperated_buffer + ".txt"); // Append "_<md5sum>.txt" to the destination path for later use while operating
        
        // Step 2: Encode the buffer to dest_path
        bool encode_status = encode_buf_to_file(dest_buffer, dest_len, dest_path);
        free(dest_buffer);
        if(false == encode_status || !file_is_present(dest_path)) {
            cerr << "encode failed for source file: " << src_path << endl;
            break;
        }

        // Step 3: Decode and Verify md5sum the destination file
        dest_buffer = nullptr;
        dest_len = 0;
        bool decode_status = decode_file_to_buffer(dest_path, dest_buffer, dest_len);
        if(false == decode_status || dest_len <= 0 || dest_buffer == nullptr) {
            cerr << "decode failed for destination file: " << dest_path << endl;
            break;
        }

        string md5sum_of_decoded_buffer;
        calculate_md5sum(dest_buffer, dest_len, md5sum_of_decoded_buffer);

        free(dest_buffer);
        dest_buffer = nullptr;

        if(md5sum_of_reoperated_buffer != md5sum_of_decoded_buffer) {
            cerr << "md5sum mismatched after reoperation and encoding" << endl;
            if(false == file_delete(dest_path)) {
                cerr << "Failed to delete dest_path after md5sum mismatch: " << dest_path << endl;
            }
            break;
        }

        overall_status = true;
    }while(false);
    
    return overall_status;
}

bool decode_operate_and_verify(const string& src_path, const string& dest_path, vector<int>&operations_status) {
    bool overall_status = false;
    int memfd = -1;

    do {
        // Step 0: Check if source file exists
        if (false == file_is_present(src_path)) {
            cerr << "Source file does not exist for decode_operate_and_verify: " << src_path << endl;
            break;
        }

        // Step 1: Decode src_path to a buffer
        unsigned char* decoded_buffer = nullptr;
        size_t decoded_len = 0;
        if (false == decode_file_to_buffer(src_path, decoded_buffer, decoded_len)) {
            cerr << "decode failed for source file: " << src_path << endl;
            break;
        }

        // Step 2: Write the decoded_buffer into in-memory temp file using createMemfdFromBuffer
        string buffer_data(reinterpret_cast<const char*>(decoded_buffer), decoded_len);
        bool memfd_status = createMemfdFromBuffer(buffer_data, memfd, "doop_tmp");

        if (memfd_status == false || memfd == -1) {
            cerr << "Failed to create in-memory file from decoded buffer" << endl;
            free(decoded_buffer);
            decoded_buffer = nullptr;
            break;
        }
        
        lseek(memfd, 0, SEEK_SET);
        std::ostringstream oss;
        oss << "/proc/self/fd/" << memfd;
        string memfd_path = oss.str();

        // Step 3: Operate on memfd_path to dest_path
        result status = nd_file_operate_to_file(memfd_path.c_str(), dest_path.c_str());
        if(status != ND_AUTH_SUCCESS) {
            cerr << "Failed to operate on memfd_path: " << memfd_path << endl;
            operations_status[3] = 0; // Mark operation as failed
            write_buf_to_file(decoded_buffer, decoded_len, dest_path); // Write the decoded buffer to dest_path
        }

        free(decoded_buffer);
        decoded_buffer = nullptr;

        // Step 4: Verify the dest file by reoperating and matching the md5sum
        unsigned char* reoperated_buffer = nullptr;
        size_t reop_len = nd_file_reoperate_to_buffer(dest_path.c_str(), &reoperated_buffer);
        if (reop_len <= 0 || reoperated_buffer == nullptr) {
            cerr << "Failed to reoperate to buffer from destination file" << endl;
            free(reoperated_buffer);
            reoperated_buffer = nullptr;
            break;
        }

        string md5sum_of_reoperated_buffer;
        calculate_md5sum(reoperated_buffer, reop_len, md5sum_of_reoperated_buffer);

        free(reoperated_buffer);
        reoperated_buffer = nullptr;
        
        // calculate the actual md5sum from the encoded path file name(src_path)
        string md5sum_of_src_path;

        // Extract md5sum from src_path filename (format: "/dev/shm/ekod_<md5sum>.txt")
        size_t underscore_pos = src_path.find_last_of('_');
        size_t dot_pos = src_path.find_last_of('.');
        if (underscore_pos != string::npos && dot_pos != string::npos && underscore_pos < dot_pos) {
            md5sum_of_src_path = src_path.substr(underscore_pos + 1, dot_pos - underscore_pos - 1);
        } else {
            cerr << "Failed to extract md5sum from src_path: " << src_path << endl;
            break; 
        }
        
        // Compare the md5sums to verify integrity
        if (md5sum_of_reoperated_buffer != md5sum_of_src_path) {
            cerr << "MD5sum mismatched after operating" << endl;
            break;
        }

        overall_status = true;
    } while (false);

    // Cleanup: close the memory file descriptor
    if (memfd != -1) {
        close(memfd);
    }
    
    return overall_status;
}

void set_file_path_from_dir(const string &file_dir, const string filename, string &file_path){ 
    vector<string> file_list;
    get_files(file_dir, file_list);

    for(vector<string>::iterator iter= file_list.begin(), end = file_list.end(); iter!=end; iter++) {
        if(strstr((*iter).c_str(), filename.c_str())) {
            file_path = file_dir + *iter;
            break;
        }
    }
}

void write_status_for_hs(const vector<int>& operations_status) { //healthstats
    cout << "sending to hs"  << endl;
    const char* status_file = is_operate ? "/home/ubuntu/config/doop_op.txt" : "/home/ubuntu/config/doop_reop.txt";
    if (file_is_present(status_file)) {
        cout << "Status file already exists, deleting it" << endl;
        file_delete(status_file);
    }

    // Create status string by concatenating all operation statuses
    string status_string = "";
    for (size_t ind = 0; ind < operations_status.size(); ++ind) {
        status_string += to_string(operations_status[ind]);
    }
    
    FILE* sfp = fopen(status_file, "w");
    if (sfp) {
        fprintf(sfp, "%s\n", status_string.c_str());
        fclose(sfp);
        cout << "Status written to file: " << status_string << endl;
    } else {
        cerr << "Failed to write status file: " << status_file << endl;
    }
}

int main(int argc, char* argv[]) {
    vector<int> operations_status = {1, 1, 1, 1}; // 0: failure, 1: success => {jwt_status, iot_status, jwt_bkp_status, is_reoperate_succeeded_for_all/is_operate_succeeded_for_all}
    
    do {
        if(!check_ota_upgrade_state()) {
            cerr << "Unexpected upgrade state" << endl;
            break;
        }

        if(!parse_args(argc, argv)){
            cerr << "Failed to parse" << endl;
            break;  
        }

        string backup_dir = "/home/ubuntu/backup/";
        set_file_path_from_dir(backup_dir, "ed25519key.pem", jwt_bkp_path);          // Get the backup path for JWT
        if(true == is_operate){
            set_file_path_from_dir(dev_shm_dir, "ekod_", jwt_encoded_tmp_path);        // Get the temporary path for JWT encoded data
            set_file_path_from_dir(dev_shm_dir, "bkod_", jwt_bkp_encoded_tmp_path);   // Get the temporary path for JWT backup encoded data
            set_file_path_from_dir(dev_shm_dir, "ptod_", iot_encoded_tmp_path);        // Get the temporary path for IOT encoded data            
        }

        vector<vector<string>> key_operations;
        
        if(false == is_operate){ // reoperation
            key_operations = {
                {jwt_cert_path, jwt_encoded_tmp_path},
                {iot_cert_path, iot_encoded_tmp_path},
                {jwt_bkp_path, jwt_bkp_encoded_tmp_path}
            };
        } else { // operation
            key_operations = {
                {jwt_encoded_tmp_path, jwt_operated_tmp_path, jwt_cert_path},
                {iot_encoded_tmp_path, iot_operated_tmp_path, iot_cert_path},
                {jwt_bkp_encoded_tmp_path, jwt_bkp_operated_tmp_path, jwt_bkp_path}
            };
        }

        // Process each operation
        for (size_t ind = 0; ind < key_operations.size(); ++ind) {            
            if(false == is_operate){ // reoperation
                if (!reoperate_encode_and_verify(key_operations[ind][0], key_operations[ind][1], operations_status)) {
                    operations_status[ind] = 0;
                    cerr << "Failed reoperate_encode_and_verify for index " << ind << ": " << key_operations[ind][0] << endl;
                }
            } else { // operation
                if (false == decode_operate_and_verify(key_operations[ind][0], key_operations[ind][1], operations_status)) {
                    operations_status[ind] = 0;
                    cerr << "Failed decode_operate_and_verify for index " << ind << ": " << key_operations[ind][0] << endl;
                }else{
                    string resp;
                    string cmd = "mv " + key_operations[ind][1] + " " + key_operations[ind][2];
                    if(false == system_execute_with_resp("DOOP", cmd, resp)){
                        operations_status[ind] = 0;
                    }
                }

                if(file_is_present(key_operations[ind][0])) {
                    if(false == file_delete(key_operations[ind][0])) {
                        cerr << "Failed to delete temporary(encoded) file: " << key_operations[ind][0] << endl;
                    }
                }

                if(file_is_present(key_operations[ind][1])) {
                    if(false == file_delete(key_operations[ind][1])) {
                        cerr << "Failed to delete temporary(encoded) file: " << key_operations[ind][1] << endl;
                    }
                }
            }
        }

        write_status_for_hs(operations_status); 

    } while(false);

    return EXIT_SUCCESS;
}
