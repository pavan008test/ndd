#include "buffer_common_utils.h"
#include <log.h>
#include <nd_file_utils.h>
#include <healthstats_utils.h>
#include "circular_buffer.h"
#include <system_utils.h>
#include "nd_factory.h"

#define TAG "BUF_CU"

#define DEBUG 0
#if DEBUG
#define CALLBACK callback
#else
#define CALLBACK NULL
#endif

#define nd_device_obj (ND_DeviceFactory::Create_NDDevice());

int callback_indx(void *index, int argc, char **argv, char **azColName);
extern NDService *nd_service_obj;
extern circular_buffer_vid *CIRC_BUFF_ctx;
// Called on file compression update
bool update_file_compression_xattr(const char* base_file_name, circular_buffer_file_compression_t fc_type);

void fill_map_add_file_healthstats(string session, string category, int status){
    json_t *root = json_object();
    json_t *category_jobj = json_object();
    json_object_set_new( category_jobj, "add_status", json_integer(status) );
    json_object_set_new( root, category.c_str(), category_jobj );
    send_video_message_healthstats(nd_service_obj, session, root);
}

void fill_map_del_file_healthstats(string session, string category, int status, int64_t starttime, int64_t endtime){
    json_t *root = json_object();
    json_t *category_jobj = json_object();
    json_object_set_new( category_jobj, "del_start", json_integer(starttime) );
    json_object_set_new( category_jobj, "del_end", json_integer(endtime) );
    json_object_set_new( category_jobj, "del_status", json_integer(status) );
    json_object_set_new( root, category.c_str(), category_jobj );
    send_video_message_healthstats(nd_service_obj, session, root);
}

int callback(void *NotUsed, int argc, char **argv, char **azColName){
   int i;
   for(i=0; i<argc; i++){
      LOG_D(TAG, "%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
   }
   LOG_D(TAG, " &&&&& ARGC count %d &&&&& ",argc);
   return 0;
}

bool update_file_compression_DB(int index, circular_buffer_file_compression_t fc_type)
{
    int rc;
    bool ret;
    string val_string;
    std::stringstream val_stream;
    val_stream.str("");
    val_stream.clear();

    circular_buffer_file_compression_t prvious_fc_type;
    val_stream << "SELECT FILE_COMPRESSION FROM VIDFILES WHERE INDEXID == '" << index << "'";
    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle,
                                val_string, callback_indx, (void*)&(prvious_fc_type));  // callback_indx is used to get compression type
    if(prvious_fc_type == CIRCULAR_BUFFER_NO_COMPRESSION || prvious_fc_type == CIRCULAR_BUFFER_NO_COMPRESSION_ADJ ) {
        LOG_D(TAG, "No need to update previous session Compression type. prvious_fc_type: %d,  fc_type: %d  ", prvious_fc_type, fc_type);
        return true;
    }

    val_stream.str("");
    val_stream.clear();
    LOG_I(TAG, " fc_type: %d  ", fc_type);
    val_stream << "UPDATE VIDFILES SET "
                << "FILE_COMPRESSION = " << fc_type
                << " WHERE INDEXID =='" << index << "'";
    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle,
                                val_string, CALLBACK, 0);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return false;
    }
    return true;
}
bool update_file_compression_DB(const char* base_file_name, circular_buffer_file_compression_t fc_type)
{
    int rc;
    bool ret;

    string val_string;
    std::stringstream val_stream;
    val_stream.str("");
    val_stream.clear();
    val_stream << "UPDATE VIDFILES SET "
                << "FILE_COMPRESSION = " << fc_type
                << " WHERE NAME =='" << base_file_name << "'";

    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle,
                                val_string, CALLBACK, 0);
    update_file_compression_xattr(base_file_name, fc_type);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return false;
    }
    // If the file has already been uploaded in video list, change the status back to CIRCULAR_BUFFER_STATUS_NEW
    // So that it can be considered as alert at cloud end as well
    val_stream.str("");
    val_stream.clear();
    val_stream << "UPDATE VIDFILES SET "
                << "STATUS = " << CIRCULAR_BUFFER_STATUS_NEW
                << " WHERE NAME =='" << base_file_name << "' AND STATUS == " << CIRCULAR_BUFFER_STATUS_NOTIFIED;
    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle,
                                val_string, CALLBACK, 0);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return false;
    }

    return true;
}

bool update_tc_status_DB(int indexid, circular_buffer_transcode_status_t status)
{
    int rc;
    bool ret;

    string val_string;
    std::stringstream val_stream;
    val_stream.str("");
    val_stream.clear();
    val_stream << "UPDATE VIDFILES SET "
                << "TRANSCODE_STATUS = " << status
                << " WHERE INDEXID==" << indexid;

    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle,
                                val_string, CALLBACK, 0);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return false;
    }
    return true;
}

bool update_tc_status_DB(const char * file_name, circular_buffer_transcode_status_t status)
{
    int rc;
    bool ret;

    string val_string;
    std::stringstream val_stream;
    val_stream.str("");
    val_stream.clear();
    val_stream << "UPDATE VIDFILES SET "
                << "TRANSCODE_STATUS = " << status
                << " WHERE NAME == '" << file_name << "'" ;

    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle,
                                val_string, CALLBACK, 0);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return false;
    }
    return true;
}

int callback_complete_fileinfo(void *fileinfo, int argc, char **argv, char **azColName){
    int i;
    const int udid_columnNum = 10, sessionCount_columnNum = 11;
    circular_buffer_file_t *temp_fileinfo = (circular_buffer_file_t *)fileinfo;
    // argc == 8 if TRANSCODE_STATUS column is not present

    if( argc < 8 )
    {
        LOG_E(TAG, "Something wrong here in callback_complete_fileinfo");
        return -1;
    }

    temp_fileinfo->index_id         = atoi(argv[0]);
    temp_fileinfo->time             = (int64_t)( strtoll( argv[1], NULL, 10 ) );
    strcpy(temp_fileinfo->base_file_name, argv[2]);
    //temp_fileinfo->duration         = atoi(argv[3]);
    temp_fileinfo->file_size         = atoi(argv[4]);

    temp_fileinfo->file_type        = (circular_buffer_filetype_t)atoi(argv[5]);
    temp_fileinfo->status           = (circular_buffer_filestatus_t)atoi(argv[6]);
    temp_fileinfo->camtype          = (circular_buffer_camtype_t)atoi(argv[7]);
    // argc == 9 if TRANSCODE_STATUS column is present
    if (argc > 8){
        temp_fileinfo->tc_status         = (circular_buffer_transcode_status_t)atoi(argv[8]);
    }
    // argc == 10 if FILE_COMPRESSION column is present
    if (argc > 9){
        temp_fileinfo->file_compression = (circular_buffer_file_compression_t)atoi(argv[9]);
    }
    if (argc > sessionCount_columnNum){
        string_to_int64( argv[udid_columnNum], temp_fileinfo->udid);
        string_to_int64( argv[sessionCount_columnNum], temp_fileinfo->sessionCount);
    }
    return 0;
}

bool hard_delete_entry_from_db(string input_file_ld)
{

    string val_string;
    std::stringstream val_stream;
    bool rc;

    val_stream.str("");
    val_stream << "DELETE FROM VIDFILES "
        << " WHERE NAME == " << "'" << input_file_ld << "'";
    val_string = val_stream.str();
    LOG_I(TAG, "SQL %s", val_string.c_str());

    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
    if(rc == false){
        LOG_E(TAG, "failed to execute in update_add_del_files_db 2");
        return false;
    }
    return true;

}

bool get_fileinfo_from_filename(circular_buffer_file_t* fileinfo, const char* base_file_name)
{
    int rc;
    bool ret = true;

    string val_string;
    std::stringstream val_stream;
    val_stream <<
                 "SELECT * FROM VIDFILES WHERE NAME == '"
                 << base_file_name << "'";

    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                     callback_complete_fileinfo, (void*)fileinfo);
    if (rc == false){
        LOG_E(TAG, "Failed to execute %s", val_string.c_str());
        return false;
    }

    return ret;
}

void update_udid_sessionCount_from_filename( circular_buffer_fileinfo_t &file_info)
{
    string fileName = file_info.base_file_name;
    file_info.udid = udid_from_file(fileName);
    file_info.sessionCount = sessionCount_from_file(fileName) ;
}

//update file compression xattr
bool update_file_compression_xattr(const char* base_file_name, circular_buffer_file_compression_t fc_type){
    if (CIRC_BUFF_ctx->extended_attr_enabled){
        ND_DeviceFactory* device = nd_device_obj;
        string filepath = device->get_external_eMMC_mount_path() + base_file_name;
        if(file_is_present(filepath)){
            if(set_file_xattr(filepath, FileMetadataKey::compr_type, &fc_type, sizeof(fc_type)) == XATTR_OK) {
                LOG_I(TAG, "Successfully updated file_compression xattr for file: %s", filepath.c_str());
            } else {
                LOG_E(TAG, "Failed to update file_compression xattr for file: %s", filepath.c_str());
                return false;
            }
        } 
    }
    return true;
}
