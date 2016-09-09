#ifndef _INSTALL_HANDLER_H_
#define _INSTALL_HANDLER_H_

#include <zlib.h>

#include "common.h"
#include "cotiny.hh"

const int32_t ZIP_FILE_SIZE = 30;
const int32_t ZIP_DIRECTORY_SIZE = 46;
const int32_t ZIP_END_BLOCK_SIZE = 22;

#ifdef _WIN32
#pragma pack(push, 2)
#endif
// header = 0x02014b50
struct ZipDirectoryHeader {
    int32_t block_header;
    int16_t ver_compress;
    int16_t ver_decompress;
    int16_t global_sig;
    int16_t comp_fun;  // only support 00-no compression 08-deflate
    int16_t mod_time;
    int16_t mod_date;
    int32_t crc32;
    int32_t comp_size;
    int32_t file_size;
    int16_t name_size;
    int16_t ex_size;
    int16_t cmt_size;
    int16_t start_disk; // generally 0
    int16_t inter_att;
    int32_t exter_att;
    int32_t data_offset;
#ifdef _WIN32
};
#pragma pack(pop)
#else
} __attribute__((packed));
#endif

#ifdef _WIN32
#pragma pack(push, 2)
#endif
// header = 0x04034b50
struct ZipFileHeader {
    int32_t block_header;
    int16_t ver_req;
    int16_t global_sig;
    int16_t comp_fun; // only support 00-no compression 08-deflate
    int16_t mod_time;
    int16_t mod_date;
    int32_t crc32;
    int32_t comp_size;
    int32_t file_size;
    int16_t name_size;
    int16_t ex_size;
#ifdef _WIN32
};
#pragma pack(pop)
#else
} __attribute__((packed));
#endif

#ifdef _WIN32
#pragma pack(push, 2)
#endif
// header = 0x06054b50
struct ZipEndBlock {
    int32_t block_header;
    int16_t disk_number; // generally 0
    int16_t directory_disk; // generally 0
    int16_t directory_count_disk;
    int16_t directory_count; // generally same as directory_count_disk
    int32_t directory_size;
    int32_t directory_offset;
    int16_t comment_size;
#ifdef _WIN32
};
#pragma pack(pop)
#else
} __attribute__((packed));
#endif

struct ZipFileInfo {
    bool compressed = false;
    size_t data_offset = 0;
    size_t comp_size = 0;
    size_t file_size = 0;
};

class InstallHandler : public PacketHandler {
public:
    ~InstallHandler() {
        if(send_routine)
            delete send_routine;
        if(send_buffer)
            delete[] send_buffer;
    }
    bool Load(const std::string& src_file) {
        char name_buffer[1024];
        ZipEndBlock end_block;
        zip_file.open(src_file, std::ios::in | std::ios::binary);
        if(!zip_file)
            return false;
        zip_file.seekg(0, zip_file.end);
        size_t file_size = zip_file.tellg();
        if(file_size == 0)
            return false;
        zip_file.seekg(-ZIP_END_BLOCK_SIZE, zip_file.end);
        zip_file.read((char*)&end_block, ZIP_END_BLOCK_SIZE);
        if(end_block.block_header != 0x06054b50) {
            int32_t end_buffer_size = (file_size >= 0xffff + ZIP_END_BLOCK_SIZE) ? (0xffff + ZIP_END_BLOCK_SIZE) : (int32_t)file_size;
            char* end_buffer = new char[end_buffer_size];
            zip_file.seekg(-end_buffer_size, zip_file.end);
            zip_file.read(end_buffer, end_buffer_size);
            int32_t end_block_pos = -1;
            for(int32_t i = end_buffer_size - 4; i >= 0; --i) {
                if(end_buffer[i] == 0x50) {
                    if(end_buffer[i + 1] == 0x4b && end_buffer[i + 2] == 0x05 && end_buffer[i + 3] == 0x06) {
                        end_block_pos = i;
                        break;
                    }
                }
            }
            delete[] end_buffer;
            if(end_block_pos == -1)
                return false;
            zip_file.seekg(-(end_buffer_size - end_block_pos), zip_file.end);
        }
        zip_file.seekg(end_block.directory_offset, zip_file.beg);
        ZipDirectoryHeader* dir_header = nullptr;
        char* buffer = new char[end_block.directory_size];
        zip_file.read(buffer, end_block.directory_size);
        auto pos = 0;
        entries.clear();
        total_size = 0;
        while(pos + ZIP_DIRECTORY_SIZE < end_block.directory_size) {
            while(buffer[pos] != 0x50 && pos + ZIP_DIRECTORY_SIZE < end_block.directory_size)
                ++pos;
            if(buffer[pos] == 0x50) {
                dir_header = reinterpret_cast<ZipDirectoryHeader*>(&buffer[pos]);
                if(dir_header->block_header != 0x02014b50)
                    continue;
                memcpy(name_buffer, &buffer[pos + ZIP_DIRECTORY_SIZE], dir_header->name_size);
                name_buffer[dir_header->name_size] = 0;
                if((dir_header->exter_att & 0x10) || (name_buffer[dir_header->name_size - 1] == '/')) { // dir
                    std::string name = name_buffer;
                    ZipFileInfo& finfo = entries[name];
                    finfo.compressed = (dir_header->comp_fun == 0x8);
                    finfo.comp_size = dir_header->comp_size;
                    finfo.file_size = dir_header->file_size;
                    finfo.data_offset = dir_header->data_offset;
                }
                pos += ZIP_DIRECTORY_SIZE + dir_header->name_size + dir_header->ex_size + dir_header->cmt_size;
                total_size += dir_header->comp_size;
            }
        }
        delete[] buffer;
        if(entries.empty())
            return false;
        if(entries.find("eboot.bin") == entries.end())
            return false;
        return true;
    }
    
    void SendAll(Sender& s) {
        static const int32_t send_threshold = 2 * 1024 * 1024;
        static const char* path_prefix = "ux0:ptmp/pkg/";
        ZipFileHeader file_header;
        VTP_VPK_CONTENT vc;
        pkt_base vc_pause = {4, 0x14};
        send_buffer_size = 0;
        for(auto& iter : entries) {
            short nlen = iter.first.length() + 13;
            int32_t csize = iter.second.comp_size;
            memcpy(&send_buffer[send_buffer_size], &nlen, 2);
            send_buffer_size += 2;
            memcpy(&send_buffer[send_buffer_size], path_prefix, 13);
            send_buffer_size += 13;
            memcpy(&send_buffer[send_buffer_size], iter.first.c_str(), iter.first.length());
            send_buffer_size += iter.first.length();
            memcpy(&send_buffer[send_buffer_size], &csize, 4);
            send_buffer_size += 4;
            zip_file.seekg(iter.second.data_offset, zip_file.beg);
            zip_file.read((char*)&file_header, ZIP_FILE_SIZE);
            zip_file.seekg(file_header.name_size + file_header.ex_size, zip_file.cur);
            int32_t bytes_left = file_header.comp_size;
            while(bytes_left != 0) {
                if(bytes_left + send_buffer_size <= send_threshold) {
                    zip_file.read((char*)&send_buffer[send_buffer_size], bytes_left);
                    send_buffer_size += bytes_left;
                    bytes_left = 0;
                } else {
                    if(send_buffer_size < send_threshold) {
                        zip_file.read((char*)&send_buffer[send_buffer_size], send_threshold - send_buffer_size);
                        bytes_left -= send_threshold - send_buffer_size;
                        send_buffer_size = send_threshold;
                    }
                    int32_t offset = 0;
                    vc.hdr.length = 1028;
                    while(offset + 1024 <= send_buffer_size) {
                        s.Send(&vc, 4);
                        s.Send(&send_buffer[offset], 1024);
                        offset += 1024;
                    }
                    if(offset != send_buffer_size) {
                        vc.hdr.length = 4 + send_buffer_size - offset;
                        s.Send(&vc, 4);
                        s.Send(&send_buffer[offset], send_buffer_size - offset);
                    }
                    send_routine->yield();
                    send_buffer_size = 0;
                }
            }
        }
        VTP_INSTALL_VPK_END ve;
        s.Send(&ve, 4);
    }
    
    void InitSend(Sender& s) {
        VTP_INSTALL_VPK iv;
        iv.hdr.length = sizeof(iv);
        iv.total_size_l = (total_size & 0xffffffff);
        iv.total_size_h = (total_size >> 32);
        iv.flag = 0;
        
        // check permission
        auto& inf = entries["eboot.bin"];
        ZipFileHeader file_header;
        zip_file.seekg(inf.data_offset, zip_file.beg);
        zip_file.read((char*)&file_header, ZIP_FILE_SIZE);
        zip_file.seekg(file_header.name_size + file_header.ex_size, zip_file.cur);
        uint8_t ebuf[1024];
        uint8_t dbuf[256];
        if(inf.compressed) {
            zip_file.read((char*)ebuf, 1024);
            z_stream estr;
            memset(&estr, 0, sizeof(estr));
            inflateInit2(&estr, -15);
            estr.next_in = ebuf;
            estr.avail_in = 1024;
            estr.avail_out = 256;
            estr.next_out = dbuf;
            inflate(&estr, Z_NO_FLUSH);
            inflateEnd(&estr);
        } else {
            zip_file.read((char*)dbuf, 256);
        }
        uint64_t authid = *(uint64_t *)(dbuf + 0x80);
        if (authid == 0x2F00000000000001 || authid == 0x2F00000000000003)
            iv.flag = 0x8;
        s.Send(&iv, iv.hdr.length);
    }
    
    int32_t HandlePacket(Sender& s, short type, void* data, int32_t length) {
        switch(type) {
            case 0x1b: {
                int32_t result = *(int32_t*)data;
                if(result != 0) {
                    if(result == 1)
                        std::cout << "Not finished yet." << std::endl;
                    else if(result == 2)
                        std::cout << "User canceled." << std::endl;
                    else
                        std::cout << "Unknown error." << std::endl;
                    return 1;
                }
                if(send_routine)
                    break;
                auto co_fun = [this, &s](cotiny::Coroutine<>* co, int32_t arg) {
                    SendAll(s);
                };
                send_buffer = new uint8_t[3 * 1024 * 1024];
                send_routine = new cotiny::Coroutine<>(co_fun, 0x10000);
                send_routine->resume();
                break;
            }
            case 0x1c: {
                if(send_routine)
                    send_routine->resume();
                break;
            }
            case 0x1d: {
                std::cout << "install success." << std::endl;
                return 1;
                break;
            }
        }
        return 0;
    }
    
protected:
    std::ifstream zip_file;
    int64_t total_size = 0;
    std::unordered_map<std::string, ZipFileInfo> entries;
    cotiny::Coroutine<>* send_routine = nullptr;
    uint8_t* send_buffer = nullptr;
    uint32_t send_buffer_size = 0;
};

#endif
