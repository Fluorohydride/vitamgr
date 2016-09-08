#include <iostream>
#include <fstream>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <vector>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <zlib.h>

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

class DataReader {
public:
    virtual int64_t Load(const std::string& src_file) = 0;
    virtual bool BeginReadFile(const std::string& filename) = 0;
    virtual uint32_t Read(uint8_t* buffer, uint32_t buffer_size) = 0;
    virtual void SetOffset(uint32_t offset) = 0;
    virtual uint32_t LeftSize() = 0;
};

class FileReader : public DataReader {
public:
    int64_t Load(const std::string& src_file) {
        file.open(src_file, std::ios::in | std::ios::binary);
        if(!file)
            return 0;
        file.seekg(0, file.end);
        file_size = file.tellg();
        return file_size;
    }
    
    bool BeginReadFile(const std::string& filename) { return true; }
    
    uint32_t Read(uint8_t* buffer, uint32_t buffer_size) {
        size_t read_left = file_size - file.tellg();
        uint32_t read_bytes = (read_left >= buffer_size) ? buffer_size : read_left;
        return file.read((char*)buffer, read_bytes);
    }
    
    void SetOffset(uint32_t offset) {
        file.seekg(offset, file.beg);
    }
    
    uint32_t LeftSize() {
        return file_size - file.tellg();
    }
    
protected:
    bool started = false;
    size_t file_size = 0;
    std::ifstream file;
};

class VpkReader : public DataReader {
public:
    int64_t Load(const std::string& src_file) {
        char name_buffer[1024];
        ZipEndBlock end_block;
        zip_file.open(src_file, std::ios::in | std::ios::binary);
        if(!zip_file)
            return 0;
        zip_file.seekg(0, zip_file.end);
        size_t file_size = zip_file.tellg();
        if(file_size == 0)
            return 0;
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
                return 0;
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
                std::string name = name_buffer;
                ZipFileInfo& finfo = entries[name];
                finfo.compressed = (dir_header->comp_fun == 0x8);
                finfo.comp_size = dir_header->comp_size;
                finfo.file_size = dir_header->file_size;
                finfo.data_offset = dir_header->data_offset;
                pos += ZIP_DIRECTORY_SIZE + dir_header->name_size + dir_header->ex_size + dir_header->cmt_size;
                total_size += finfo.comp_size;
            }
        }
        delete[] buffer;
        read_left = 0;
        if(entries.empty())
            return 0;
        return total_size;
    }
    
    bool BeginReadFile(const std::string& filename) {
        if(!zip_file)
            return false;
        auto iter = entries.find(filename);
        if(iter == entries.end())
            return false;
        auto& file_info = iter->second;
        ZipFileHeader file_header;
        zip_file.seekg(file_info.data_offset, zip_file.beg);
        zip_file.read((char*)&file_header, ZIP_FILE_SIZE);
        if(file_header.block_header != 0x04034b50)
            return false;
        zip_file.seekg(file_header.name_size + file_header.ex_size, zip_file.cur);
        read_left = file_info.comp_size;
        return true;
    }
    
    uint32_t Read(uint8_t* buffer, uint32_t buffer_size) {
        if(read_left <= 0)
            return 0;
        uint32_t read_bytes = (read_left >= buffer_size) ? buffer_size : read_left;
        zip_file.read((char*)buffer, read_bytes);
        read_left -= read_bytes;
        return read_bytes;
    }
    
    void SetOffset(uint32_t offset) {}
    
    uint32_t LeftSize() {
        return read_left;
    }
    
    ZipFileInfo* GetInfo(const std::string& filename) {
        auto iter = entries.find(filename);
        if(iter == entries.end())
            return nullptr;
        return &iter->second;
    }
    
    std::pair<const std::string*, ZipFileInfo*> FirstFileInfo() {
        iter = entries.begin();
        count = 0;
        if(iter != entries.end())
            return std::make_pair(&iter->first, &iter->second);
        return std::make_pair(nullptr, nullptr);
    }
    
    std::pair<const std::string*, ZipFileInfo*> NextFileInfo() {
        if(iter != entries.end()) {
            iter++;
            count++;
        }
        if(iter != entries.end())
            return std::make_pair(&iter->first, &iter->second);
        return std::make_pair(nullptr, nullptr);
    }
    
    std::pair<int32_t, int32_t> Prog() { return std::make_pair(count, entries.size()); }
    
protected:
    int32_t count = 0;
    std::ifstream zip_file;
    int64_t total_size = 0;
    int64_t read_left = 0;
    std::unordered_map<std::string, ZipFileInfo> entries;
    std::unordered_map<std::string, ZipFileInfo>::iterator iter;
};


struct pkt_base {
    short length;
    short type;
};

struct VTP_BEGIN_FILE {
    pkt_base hdr = {8, 0x10};
    uint32_t size;
    uint32_t flag;
    char name[256];
};

struct VTP_FILE_CONTENT {
    pkt_base hdr = {4, 0x11};
    uint8_t buf[1024];
};

struct VTP_FILE_END {
    pkt_base hdr = {4, 0x12};
};

struct VTP_INSTALL_VPK {
    pkt_base hdr = {16, 0x13};
    uint32_t total_size_l = 0;
    uint32_t total_size_h = 0;
    uint32_t flag = 0;
};

struct VTP_INSTALL_VPK_END {
    pkt_base hdr = {4, 0x14};
};

struct VTRP_RES {
    short length = 8;
    short type = 0;
    int result = 0;
};

void send_resp(int client, short type, int result) {
    VTRP_RES res;
    res.type = type;
    res.result = result;
    send(client, &res, 8, 0);
}

int32_t mode = 0;

int32_t handle_packet(DataReader* dr, int client, short type, void* data, int32_t length) {
    switch(type) {
        case 0x18: {
            int32_t result = *(int32_t*)data;
            if(result != 0) {
                if(result == 1)
                    std::cout << "Not finished yet." << std::endl;
                else if(result == 2)
                    std::cout << "User canceled." << std::endl;
                else if(result == 3)
                    std::cout << "Cannot create path." << std::endl;
                else if(result == 4)
                    std::cout << "Cannot open file." << std::endl;
                else
                    std::cout << "Unknown error." << std::endl;
                return 1;
            }
            break;
        }
        case 0x19: {
            int32_t offset = *(int32_t*)data;
            dr->SetOffset(offset);
            uint32_t sz_to_end = dr->LeftSize();
            if(sz_to_end == 0) {
                VTP_FILE_END fe;
                send(client, &fe, 4, 0);
            }
            VTP_FILE_CONTENT fc;
            if(sz_to_end >= 2 * 1024 * 1024) {
                // send 2mb
                fc.hdr.length = 4 + 1024;
                for(int32_t i = 0; i < 2048; ++i) {
                    dr->Read(fc.buf, 1024);
                    send(client, &fc, 4 + 1024, 0);
                }
                fc.hdr.length = 4;
                send(client, &fc, 4, 0);
            } else {
                fc.hdr.length = 4 + 1024;
                for(int32_t i = 0; i < sz_to_end / 1024; ++i) {
                    dr->Read(fc.buf, 1024);
                    send(client, &fc, 4 + 1024, 0);
                }
                fc.hdr.length = 4 + (sz_to_end % 1024);
                dr->Read(fc.buf, sz_to_end % 1024);
                send(client, &fc, fc.hdr.length, 0);
                VTP_FILE_END fe;
                send(client, &fe, 4, 0);
            }
            break;
        }
        case 0x1a: {
            if(mode == 1) {
                std::cout << "done." << std::endl;
                return 1;
            }
            VpkReader* vr = static_cast<VpkReader*>(dr);
            auto prog = vr->Prog();
            std::cout << "done. " << prog.first + 1 << "/" << prog.second << std::endl;
            auto inf = vr->NextFileInfo();
            if(inf.first != nullptr) {
                std::cout << "begin upload " << *inf.first << " ... ";
                vr->BeginReadFile(inf.first->c_str());
                VTP_BEGIN_FILE bf;
                sprintf(bf.name, "ux0:ptmp/pkg/%s", inf.first->c_str());
                bf.hdr.length = 12 + strlen(bf.name) + 1;
                bf.hdr.type = 0x10;
                bf.size = inf.second->comp_size;
                bf.flag = (inf.second->compressed) ? (0x1 + 0x2) : 0x1;
                send(client, &bf, bf.hdr.length, 0);
            } else {
                // all file sent
                VTP_INSTALL_VPK_END ve;
                send(client, &ve, 4, 0);
            }
            break;
        }
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
            VpkReader* vr = static_cast<VpkReader*>(dr);
            std::cout << "install begin." << std::endl;
            auto inf = vr->FirstFileInfo();
            std::cout << "begin upload " << *inf.first << " ... ";
            vr->BeginReadFile(inf.first->c_str());
            VTP_BEGIN_FILE bf;
            sprintf(bf.name, "ux0:ptmp/pkg/%s", inf.first->c_str());
            bf.hdr.length = 12 + strlen(bf.name) + 1;
            bf.hdr.type = 0x10;
            bf.size = inf.second->comp_size;
            bf.flag = (inf.second->compressed) ? (0x1 + 0x2) : 0x1;
            send(client, &bf, bf.hdr.length, 0);
            break;
        }
        case 0x1c: {
            std::cout << "install success." << std::endl;
            return 1;
            break;
        }
    }
    return 0;
}

void show_usage(char* cmd) {
    std::cout << cmd << " [ip] copy [local_file] [remote_file]" << std::endl;
    std::cout << cmd << " [ip] install [local_vpk]" << std::endl;
}

int32_t main(int32_t argc, char* argv[]) {
    if(argc < 3) {
        show_usage(argv[0]);
        return 0;
    }
    DataReader* dr = nullptr;
    size_t max_sz = 0;
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1340);
    addr.sin_addr.s_addr = inet_addr(argv[1]);
    if(addr.sin_addr.s_addr == 0xffffffff) {
        show_usage(argv[0]);
        return 0;
    }
    if(strcmp(argv[2], "copy") == 0) {
        if(argc < 5) {
            show_usage(argv[0]);
            return 0;
        }
        dr = new FileReader();
        max_sz = dr->Load(argv[3]);
        if(max_sz == 0) {
            std::cout << "local file " << argv[3] << " not exists." << std::endl;
            return 0;
        }
        mode = 1;
    } else if(strcmp(argv[2], "install") == 0) {
        if(argc < 4) {
            show_usage(argv[0]);
            return 0;
        }
        auto vr = new VpkReader();
        max_sz = vr->Load(argv[3]);
        if(max_sz == 0) {
            std::cout << "local vpk " << argv[3] << " not exists." << std::endl;
            return 0;
        }
        auto inf = vr->GetInfo("eboot.bin");
        if(!inf) {
            std::cout << "local vpk " << argv[3] << " cracked." << std::endl;
            return 0;
        }
        dr = vr;
        mode = 2;
    } else {
        show_usage(argv[0]);
        return 0;
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int res = connect(sock, (sockaddr*)&addr, sizeof(addr));
    if(res == 0) {
        char recv_buffer[8192];
        int recv_offset = 0;
        bool quit = false;
        std::cout << "server connected." << std::endl;
        // first pack
        if(mode == 1) {
            VTP_BEGIN_FILE bf;
            sprintf(bf.name, "%s", argv[4]);
            bf.hdr.length = 12 + strlen(bf.name) + 1;
            bf.hdr.type = 0x10;
            bf.size = max_sz;
            bf.flag = 0x1;
            send(sock, &bf, bf.hdr.length, 0);
        } else if(mode == 2) {
            VTP_INSTALL_VPK iv;
            iv.hdr.length = sizeof(iv);
            iv.total_size_l = (max_sz & 0xffffffff);
            iv.total_size_h = (max_sz >> 32);
            iv.flag = 0;

            VpkReader* vr = (VpkReader*)dr;
            auto inf = vr->GetInfo("eboot.bin");
            // check permission
            dr->BeginReadFile("eboot.bin");
            uint8_t ebuf[1024];
            uint8_t dbuf[256];
            if(inf->compressed) {
                dr->Read(ebuf, 1024);
                z_stream estr;
                memset(&estr, 0, sizeof(estr));
                inflateInit2(&estr, -15);
                estr.next_in = ebuf;
                estr.avail_in = 1024;
                estr.avail_out = 256;
                estr.next_out = dbuf;
                inflate(&estr, Z_NO_FLUSH);
            } else {
                dr->Read(dbuf, 256);
            }
            uint64_t authid = *(uint64_t *)(dbuf + 0x80);
            if (authid == 0x2F00000000000001 || authid == 0x2F00000000000003)
                iv.flag = 0x8;
            send(sock, &iv, iv.hdr.length, 0);
        }
        pkt_base hdr;
        // begin recv
        while (!quit) {
            int recv_size = recv(sock, &recv_buffer[recv_offset], 8192 - recv_offset, 0);
            if (recv_size > 0) {
                recv_offset += recv_size;
                int offset = 0;
                while(offset + 4 <= recv_offset) {
                    int left_data_size = recv_offset - offset;  // include header
                    memcpy(&hdr, &recv_buffer[offset], 4);
                    if(hdr.length < 4 || hdr.length > 1500) {
                        // packet length error, skip
                        offset += 2;
                        continue;
                    };
                    if(hdr.length > left_data_size) {
                        // need receive more data
                        break;
                    }
                    if(handle_packet(dr, sock, hdr.type, &recv_buffer[offset + 4], hdr.length - 4)) {
                        quit = true;
                        break;
                    }
                    offset += hdr.length;
                }
                if(offset != recv_offset)
                    memmove(recv_buffer, &recv_buffer[offset], recv_offset - offset);
                recv_offset -= offset;
            } else {
                // =0 -- connection closed
                // <0 -- error
                break;
            }
        }
    }
    close(sock);
    return 0;
}
