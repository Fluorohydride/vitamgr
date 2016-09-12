#ifndef _DOWN_HANDLER_H_
#define _DOWN_HANDLER_H_

#include "common.h"
#include "cotiny.hh"

class CopyHandler : public PacketHandler {
public:
    ~CopyHandler() {
        if(send_routine)
            delete send_routine;
    }
    
    bool Load(const std::string& src_file, const std::string& remote_path) {
        file.open(src_file, std::ios::in | std::ios::binary);
        if(!file)
            return false;
        file.seekg(0, file.end);
        file_size = file.tellg();
        vita_path = remote_path;
        return true;
    }
    
    void SendAll(Sender& s, int32_t offset) {
        static const int32_t send_threshold = 2 * 1024 * 1024;
        file.seekg(offset, file.beg);
        VTP_FILE_CONTENT fc;
        pkt_base fc_pause = {4, 0x11};
        file.read((char*)fc.buf, 1024);
        size_t bytes_read = file.gcount();
        size_t bytes_sum = 0;
        while(bytes_read > 0) {
            fc.hdr.length = 4 + bytes_read;
            s.Send(&fc, fc.hdr.length);
            bytes_sum += bytes_read;
            if(bytes_sum >= send_threshold) {
                bytes_sum = 0;
                s.Send(&fc_pause, 4);
                send_routine->yield();
            }
            file.read((char*)fc.buf, 1024);
            bytes_read = file.gcount();
        }
        VTP_FILE_END fe;
        s.Send(&fe, 4);
    }
    
    void InitSend(Sender& s) {
        VTP_DOWN_FILE df;
        bf.hdr.length = 12 + vita_path.length() + 1;
        bf.hdr.type = 0x10;
        bf.size = file_size;
        bf.flag = 0x1;
        s.Send(&bf, 12);
        s.Send((void*)vita_path.c_str(), vita_path.length() + 1);
    }
    
    int32_t HandlePacket(Sender& s, short type, void* data, int32_t length) {
        switch(type) {
            case 0x13: {
                int32_t result = ((int32_t*)data)[0];
                int32_t fsize = ((int32_t*)data)[1];
                if(result != 0) {

                }
                
            }
            case 0x14: {
                if(send_routine)
                    send_routine->resume();
                break;
            }
            case 0x15: {
                std::cout << "done." << std::endl;
                return 1;
                break;
            }
        }
        return 0;
    }

protected:
    size_t send_res = 0;
    size_t file_size = 0;
    std::ifstream file;
    std::string vita_path;
    cotiny::Coroutine<>* send_routine = nullptr;
};

#endif
