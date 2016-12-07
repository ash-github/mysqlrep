/***************************************************************************
 * 
 * Copyright (c) 2014 Sina.com, Inc. All Rights Reserved
 * 
 **************************************************************************/



/**
 * @file bus_event.cc
 * @author liudong2(liudong2@staff.sina.com.cn)
 * @date 2014/05/15 15:41:13
 * @version $Revision$
 * @brief 
 *  
 **/
#include <time.h>
#include "bus_event.h"
#include "mydecimal.h"
#include "myconvert.h"
#include "bus_config.h"
#include "bus_util.h"
#include "my_time.h"

namespace bus {
    //bus_config_t g_config;

#if 1
    /*
     * @brief 解析table map event中meta数据
     */
    uint16_t read_meta(bus_mem_block_t &buf, uint32_t &pos, uint8_t datatype)
    {
        uint16_t meta = 0;
        if (datatype == MYSQL_TYPE_TINY_BLOB ||
                datatype == MYSQL_TYPE_BLOB ||
                datatype == MYSQL_TYPE_MEDIUM_BLOB ||
                datatype == MYSQL_TYPE_LONG_BLOB ||
                datatype == MYSQL_TYPE_DOUBLE ||
                datatype == MYSQL_TYPE_FLOAT ||
                datatype == MYSQL_TYPE_GEOMETRY ||
                datatype == MYSQL_TYPE_TIME2 ||
                datatype == MYSQL_TYPE_DATETIME2 ||
                datatype == MYSQL_TYPE_TIMESTAMP2) {
            meta = (uint16_t)(uint8_t)buf.at(pos);
            pos += 1;
        } else if(datatype == MYSQL_TYPE_NEWDECIMAL ||
                datatype == MYSQL_TYPE_VAR_STRING ||
                datatype == MYSQL_TYPE_STRING ||
                datatype == MYSQL_TYPE_SET ||
                datatype == MYSQL_TYPE_ENUM) {
            meta = ((uint16_t)(uint8_t)buf.at(pos) << 8) | ((uint16_t)(uint8_t)buf.at(pos + 1));
            pos += 2;
        } else if (datatype == MYSQL_TYPE_VARCHAR) {
            meta = uint2korr(buf.block + pos);
            pos += 2;
        }else if (datatype == MYSQL_TYPE_BIT) {
            /*
             *@todo 第一位 length%8 第二位 length/8
             */
            meta = ((uint16_t)(uint8_t)buf.at(pos) << 8) | ((uint16_t)(uint8_t)buf.at(pos + 1));
            printf("bit metadata %x, %x\n", (uint16_t)buf.at(pos), (uint16_t)buf.at(pos + 1));
            pos += 2;

        }else if (datatype == MYSQL_TYPE_DECIMAL ||
                datatype == MYSQL_TYPE_TINY ||
                datatype == MYSQL_TYPE_SHORT ||
                datatype == MYSQL_TYPE_LONG ||
                datatype == MYSQL_TYPE_NULL ||
                datatype == MYSQL_TYPE_TIMESTAMP ||
                datatype == MYSQL_TYPE_LONGLONG ||
                datatype == MYSQL_TYPE_INT24 ||
                datatype == MYSQL_TYPE_DATE ||
                datatype == MYSQL_TYPE_TIME ||
                datatype == MYSQL_TYPE_DATETIME ||
                datatype == MYSQL_TYPE_YEAR) {
            meta = 0;
        }else {
            g_logger.error("[datatype=%d] can't exist in the binlog");
        }

        return meta;
    }
#endif

    /*
     * @brief 解析压缩过的整型数据 
     */
    bool unpack_int(char *p, uint32_t &npos, uint64_t &value){
        uint8_t type = p[0];
        if(type < 0xfb){
            npos++;
            value = type;
        }else if(type == 0xfc){
            //npos += 2;
            //value = uint2korr(p);
            npos += 3;
            value = uint2korr(p+1);
        }else if(type == 0xfd){
            //npos += 3;
            //value = uint3korr(p);
            npos += 4;
            value = uint3korr(p+1);
        }else if(type == 0xfe){
            //npos += 8;
            //value = uint8korr(p);
            npos += 9;
            value = uint8korr(p+1);
        }else {
            g_logger.error("unkown integer type");
            return false;
        }

        return true;
    }


    /*
     * 解析event中的column数据
     */
    bool parse_column_value(char *buf, uint8_t type, uint16_t meta, uint32_t &pos, row_t *row, bool is_old, convert_charset_t &convert, column_t *col)
    {
        uint32_t length= 0;
        //转换类型，将MYSQL_TYPE_STRING类型转化为具体的类型
        if (type == MYSQL_TYPE_STRING)
        {
            if (meta >= 256)
            {
                uint8_t byte0= meta >> 8;
                uint8_t byte1= meta & 0xFF;

                if ((byte0 & 0x30) != 0x30)
                {
                    /* a long CHAR() field: see #37426 https://bugs.mysql.com/bug.php?id=37426*/
                    length= byte1 | (((byte0 & 0x30) ^ 0x30) << 4);
                    type= byte0 | 0x30;
                }
                else
                {
                    switch (byte0)
                    {
                        case MYSQL_TYPE_SET:
                        case MYSQL_TYPE_ENUM:
                        case MYSQL_TYPE_STRING:
                            type = byte0;
                            length = byte1;
                            break;
                        default:
                            g_logger.error("don't know how to handle column type=%d meta=%d, byte0=%d, byte1=%d", 
                                    type, meta, byte0, byte1);
                            return false;
                    }
                }
            } else {
                length= meta;
            }
        }

        //根据具体的类型, 以及元数据进行转化
        int col_unsigned = -1;
        //if (col != NULL) col_unsigned = col->get_column_type();
        char value[256];
        switch(type)
        {
            case MYSQL_TYPE_LONG: //int32
                {
                    if (col_unsigned == 1) {
                        uint32_t i = uint4korr(buf); 
                        snprintf(value, sizeof(value), "%u", i);
                    } else {
                        int32_t i = sint4korr(buf); 
                        snprintf(value, sizeof(value), "%d", i);
                    }
                    pos += 4;
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_TINY:
                {
                    if (col_unsigned == 1) {
                        uint8_t c = (uint8_t)(*buf);
                        snprintf(value, sizeof(value), "%hhu", c);
                    } else {
                        int8_t c = (int8_t)(*buf);
                        snprintf(value, sizeof(value), "%hhd", c);
                    }
                    pos += 1;
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_SHORT:
                {
                    if (col_unsigned == 1) {
                        uint16_t s = uint2korr(buf);
                        snprintf(value, sizeof(value), "%hu", s);
                    } else {
                        int16_t s = sint2korr(buf);
                        snprintf(value, sizeof(value), "%hd", s);
                    }
                    pos += 2;
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_LONGLONG:
                {
                    if (col_unsigned == 1) {
                        int64_t ll = uint8korr(buf);
                        snprintf(value, sizeof(value), "%lu", ll);
                    } else {
                        uint64_t ll = sint8korr(buf);
                        snprintf(value, sizeof(value), "%ld", ll);
                    }
                    pos += 8;
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_INT24:
                {
                    int32_t i;
                    if (col_unsigned == 1) {
                        i = uint3korr(buf); 
                    } else {
                        i = sint3korr(buf); 
                    }
                    pos += 3;
                    snprintf(value, sizeof(value), "%d", i);
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_TIMESTAMP:
                {
                    uint32_t i = uint4korr(buf);
                    pos += 4;
                    snprintf(value, sizeof(value), "%u", i);
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_DATETIME:
                {
                    uint64_t ll = uint8korr(buf);
                    uint32_t d = ll / 1000000;
                    uint32_t t = ll % 1000000;
                    snprintf(value, sizeof(value),
                            "%04d-%02d-%02d %02d:%02d:%02d",
                            d / 10000, (d % 10000) / 100, d % 100,
                            t / 10000, (t % 10000) / 100, t % 100);
                    pos += 8;
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_TIME:
                {
                    uint32_t i32= uint3korr(buf);
                    snprintf(value,
                            sizeof(value),
                            "%02d:%02d:%02d",
                            i32 / 10000, (i32 % 10000) / 100, i32 % 100);
                    pos += 3;
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_NEWDATE:
                {
                    uint32_t tmp= uint3korr(buf);
                    int part;
                    char sbuf[11];
                    char *spos= &sbuf[10];  // start from '\0' to the beginning

                    /* Copied from field.cc */
                    *spos--=0;                 // End NULL
                    part=(int) (tmp & 31);
                    *spos--= (char) ('0'+part%10);
                    *spos--= (char) ('0'+part/10);
                    *spos--= ':';
                    part=(int) (tmp >> 5 & 15);
                    *spos--= (char) ('0'+part%10);
                    *spos--= (char) ('0'+part/10);
                    *spos--= ':';
                    part=(int) (tmp >> 9);
                    *spos--= (char) ('0'+part%10); part/=10;
                    *spos--= (char) ('0'+part%10); part/=10;
                    *spos--= (char) ('0'+part%10); part/=10;
                    *spos=   (char) ('0'+part);

                    snprintf(value, sizeof(value), "%s", sbuf);
                    pos += 3;
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_DATE:
                {
                    uint32_t i32 = uint3korr(buf);
                    snprintf(value,
                            sizeof(value),
                            "%04d-%02d-%02d",
                            (int32_t)(i32 / (16L * 32L)), (int32_t)(i32 / 32L % 16L), (int32_t)(i32 % 32L));
                    pos += 3;
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_YEAR:
                {
                    uint32_t i = (uint32_t)(uint8_t)*buf;
                    snprintf(value, sizeof(value), "%04d", i + 1900);
                    pos += 1;
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_ENUM:
                {
                    switch (length) {
                        case 1:
                            snprintf(value, sizeof(value), "%d", (int32_t)*buf);
                            pos += 1;
                            row->push_back(value, is_old);
                            break;
                        case 2:
                            {
                                int32_t i32 = uint2korr(buf);
                                snprintf(value, sizeof(value) ,"%d", i32);
                                pos += 2;
                                row->push_back(value, is_old);
                            }
                            break;
                        default:
                            g_logger.error("unknown enum packlen=%d", length);
                            return false;
                    }
                }
                break;
            case MYSQL_TYPE_SET:
                {
                    pos += length;
                    row->push_back(buf, length, is_old);
                }
                break;
            case MYSQL_TYPE_BLOB:
                switch (meta) 
                {
                    case 1:     //TINYBLOB/TINYTEXT
                        {
                            length = (uint8_t)(*buf);
                            pos += length + 1;
                            row->push_back(buf + 1, length, is_old);
                        }
                        break;
                    case 2:     //BLOB/TEXT
                        {
                            length = uint2korr(buf);
                            pos += length + 2;
                            row->push_back(buf + 2, length, is_old);
                        }
                        break;
                    case 3:     //MEDIUMBLOB/MEDIUMTEXT
                        {
                            length = uint3korr(buf);
                            pos += length + 3;
                            row->push_back(buf + 3, length, is_old);
                        }
                        break;
                    case 4:     //LONGBLOB/LONGTEXT
                        {
                            length = uint4korr(buf);
                            pos += length + 4;
                            row->push_back(buf + 4, length, is_old);
                        }
                        break;
                    default:
                        g_logger.error("unknown blob type=%d", meta);
                        return false;
                }
                break;
            case MYSQL_TYPE_VARCHAR:
            case MYSQL_TYPE_VAR_STRING:
                {
                    length = meta;
                    if (length < 256){
                        length = (uint8_t)(*buf);
                        pos += 1 + length;
                        if (!convert.convert_charset(buf + 1, length, row, is_old))
                        {
                            g_logger.error("parse string column value fail, convert_charset fail");
                            return true;
                        }
                        //row->push_back(buf + 1, length, is_old);
                    } else {
                        length = uint2korr(buf);
                        pos += 2 + length;
                        if (!convert.convert_charset(buf + 2, length, row, is_old))
                        {
                            g_logger.error("parse string column value fail, convert_charset fail");
                            return true;
                        }
                        //row->push_back(buf + 2, length, is_old);
                    }
                }
                break;
            case MYSQL_TYPE_STRING:
                {
                    if (length < 256){
                        length = (uint8_t)(*buf);
                        pos += 1 + length;
                        //row->push_back(buf + 1, length, is_old);
                        if (!convert.convert_charset(buf + 1, length, row, is_old))
                        {
                            g_logger.error("parse string column value fail, convert_charset fail");
                            return true;
                        }
                    }else{
                        length = uint2korr(buf);
                        pos += 2 + length;
                        //row->push_back(buf + 2, length, is_old);
                        if (!convert.convert_charset(buf + 2, length, row, is_old))
                        {
                            g_logger.error("parse string column value fail, convert_charset fail");
                            return true;
                        }
                    }
                }
                break;
            case MYSQL_TYPE_BIT:
                {
                    uint32_t nBits= (meta >> 8)  + (meta & 0xFF) * 8;
                    length= (nBits + 7) / 8;
                    pos += length;
                    row->push_back(buf, length, is_old);
                }
                break;
            case MYSQL_TYPE_FLOAT:
                {
                    float fl;
                    float4get(fl, buf);
                    pos += 4;
                    snprintf(value, sizeof(value), "%f", (double)fl);
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_DOUBLE:
                {
                    double dbl;
                    float8get(dbl,buf);
                    pos += 8;
                    snprintf(value, sizeof(value), "%f", dbl);
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_NEWDECIMAL:
                {
                    uint8_t precision= meta >> 8;
                    uint8_t decimals= meta & 0xFF;
                    int bin_size = my_decimal_get_binary_size(precision, decimals);
                    my_decimal dec;
                    binary2my_decimal(E_DEC_FATAL_ERROR, (unsigned char*) buf, &dec, precision, decimals);
                    int i, end;
                    char buff[512], *spos;
                    spos = buff;
                    spos += sprintf(buff, "%s", dec.sign() ? "-" : "");
                    end= ROUND_UP(dec.frac) + ROUND_UP(dec.intg)-1;
                    for (i=0; i < end; i++)
                        spos += sprintf(spos, "%09d.", dec.buf[i]);
                    spos += sprintf(spos, "%09d", dec.buf[i]);
                    row->push_back(buff, is_old);
                    pos += bin_size;
                }
                break;
            case MYSQL_TYPE_TIMESTAMP2: //mysql 5.6新增类型
                {
                    struct timeval tm;
                    my_timestamp_from_binary(&tm, buf, pos,  meta);
                    snprintf(value, sizeof(value), "%u.%u", (uint32_t)tm.tv_sec, (uint32_t)tm.tv_usec);
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_DATETIME2:  //mysql 5.6新增类型
                {
                    int64_t ymd, hms;
                    int64_t ymdhms, ym;
                    int frac;
                    int64_t packed= my_datetime_packed_from_binary(buf, pos, meta);
                    if (packed < 0)
                        packed = -packed;

                    ymdhms= MY_PACKED_TIME_GET_INT_PART(packed);
                    frac = MY_PACKED_TIME_GET_FRAC_PART(packed);

                    ymd= ymdhms >> 17;
                    ym= ymd >> 5;
                    hms= ymdhms % (1 << 17);

                    int day= ymd % (1 << 5);
                    int month= ym % 13;
                    int year= ym / 13;

                    int second= hms % (1 << 6);
                    int minute= (hms >> 6) % (1 << 6);
                    int hour= (hms >> 12);

                    snprintf(value, sizeof(value), "%04d-%02d-%02d %02d:%02d:%02d.%d",
                            year, month, day, hour, minute, second, frac);
                    row->push_back(value, is_old);
                }
                break;
            case MYSQL_TYPE_TIME2: //mysql 5.6新增类型
                {
                    assert(meta <= DATETIME_MAX_DECIMALS);
                    int64_t packed= my_time_packed_from_binary(buf, pos, meta);
                    if (packed < 0)
                        packed = -packed;

                    long hms = MY_PACKED_TIME_GET_INT_PART(packed);
                    int frac = MY_PACKED_TIME_GET_FRAC_PART(packed);

                    int hour= (hms >> 12) % (1 << 10);
                    int minute= (hms >> 6)  % (1 << 6);
                    int second= hms        % (1 << 6);
                    snprintf(value, sizeof(value), "%02d:%02d:%02d.%d", hour, minute, second, frac);
                    row->push_back(value, is_old);
                }
                break;

            default:
                g_logger.error("don't know how to handle type =%d, meta=%d column value", type, meta);
                return false;
        }
        return true;
    }

    bus_packet_t::bus_packet_t(bool bIsChecksumEnable):
        _bodylen(0),_seqid(0),event_pack(bIsChecksumEnable)
    {
        _body = (char*)malloc(PACKET_NORMAL_SIZE);
        if (_body == NULL) oom_error();
        _body_mem_sz = PACKET_NORMAL_SIZE;

        m_bIsChecksumEnable = bIsChecksumEnable;
    }

    bus_packet_t::~bus_packet_t()
    {
        if (_body != NULL)
        {
            free(_body);
            _body = NULL;
        }
        _body_mem_sz = 0;
    }
    void bus_packet_t::_add_mem(uint32_t sz)
    {
        if (_bodylen + sz > _body_mem_sz)
        {
            char *temp = (char *)realloc(_body, _bodylen + sz);
            if (temp == NULL) oom_error();
            _body = temp;
            _body_mem_sz = _bodylen + sz;
        }
    }
    int bus_packet_t::read_packet(int fd)
    {
        int cur_payload_sz = 0xffffff;
        _bodylen = 0;
        int bytes_ct, ret;
        while (cur_payload_sz == 0xffffff)
        {
            /* 读取packet head */
            char buf[4];
            bytes_ct = 0;
            while (bytes_ct != 4) {
                ret = read(fd, buf + bytes_ct, 4 - bytes_ct);
                if (ret == 0) {
                    g_logger.notice("read socket eof");
                    return -1;
                } else if (ret == -1) {
                    g_logger.error("read packet head fail, error:%s", strerror(errno));
                    return -1;
                } else {
                    bytes_ct += ret;
                }
            }

            cur_payload_sz = uint3korr(buf);
            /* 分配空间 */
            _add_mem(cur_payload_sz);
            /*读取body */
            bytes_ct = 0;
            while(bytes_ct != cur_payload_sz) {
                ret = read(fd, _body + _bodylen + bytes_ct, cur_payload_sz - bytes_ct);
                if (ret == 0) {
                    g_logger.notice("read socket eof");
                    return -1;
                } else if (ret == -1) {
                    g_logger.error("read packet body fail, error:%s", strerror(errno));
                    return -1;
                } else {
                    //g_logger.notice("sz=%d,count=%d", cur_payload_sz, ret);
                    bytes_ct += ret;
                }
            }
            _bodylen += cur_payload_sz;
        }
        //g_logger.notice("read %d",_bodylen);
        return 0;
    }

    int32_t bus_packet_t::parse_packet(bus_user_process *pUserProcess)
    {
        uint32_t pos = 0;
        uint8_t pack_type = _body[0];
        pos += 1;

        int ret;
        if (pack_type == 0x00)
        {
            ret = event_pack.parse_event(_body, pos, _bodylen, pUserProcess);
            if (ret == -1) {
                g_logger.error("parse event fail");
                return -1;
            } else if (ret == 1) {
                g_logger.debug("recv stop signal");
                return 1;
            }
        } else if (pack_type == 0xff) {
            ret = error_pack.parse(_body, pos, _bodylen); 
            if (ret != 0)
            {
                g_logger.error("parse error packet fail");
                return -1;
            }
            error_pack.print_info();
            assert(pos == _bodylen);
            return -1;
        } else if (pack_type == 0xfe) {
            g_logger.debug("read eof packet");
            return 2;
        }
        return 0;
    }
    /***********************************************************/
    int32_t bus_error_packet_t::parse(char *buf, uint32_t &pos, uint32_t pack_len)
    {
        _errcode = uint2korr(buf + pos);
        pos += 2;
        memcpy(_state, buf + pos, sizeof(_state));
        pos += sizeof(_state);

        uint32_t left = pack_len - pos;
        _info.assign(buf + pos, left);
        pos += left;
        return 0;
    }
    /***********************************************************/
    int32_t bus_dump_cmd_t::write_packet(int fd)
    {
        char buf[1024];
        uint32_t bodysz = 0;
        uint32_t offset = 4;
        buf[offset] = _cmd;
        bodysz += 1;
        int4store(buf + offset + bodysz, _binlog_pos);
        bodysz += 4;
        int2store(buf + offset + bodysz, _flags);
        bodysz += 2;
        int4store(buf + offset + bodysz, _server_id);
        bodysz += 4;

        std::size_t binlog_filename_len = _binlog_filename.size();
        memcpy(buf + offset + bodysz, _binlog_filename.c_str(), binlog_filename_len);
        bodysz += binlog_filename_len;

        /* set seq id */
        buf[offset - 1] = 0x00;

        int3store(buf, bodysz);

        int ret = write(fd, buf, bodysz + 4);
        if (ret != int(bodysz + 4))
        {
            g_logger.error("write dump cmd packet fail, error:%s", strerror(errno));
            return -1;
        }
        return 0;
    }
    /**********************************************************/
    void bus_event_head_t::parse_event_head(char *buf, uint32_t &pos)
    {
        time_stamp = uint4korr(buf + pos);
        pos += 4;
        event_type = buf[pos];
        pos += 1;
        server_id = uint4korr(buf + pos);
        pos += 4;
        event_sz = uint4korr(buf + pos);
        pos += 4;
        log_pos = uint4korr(buf + pos);
        pos += 4;
        flags = uint2korr(buf + pos);
        pos += 2;
    }
    /**********************************************************/

    bool bus_event_t::init_event_schema_charset(bus_table_map_t *pMapEvent)
    {
        std::string& database_name = pMapEvent->get_database_name();
        std::string& table_name = pMapEvent->get_table_name();
        map_schema = g_config.get_match_schema(database_name.c_str(),table_name.c_str());

        if (map_charset == NULL)
            map_charset = new std::string("utf-8");

        const char *target_charset = g_config.get_target_charset();
        if (map_charset != NULL && target_charset != NULL)
        {
            if (!map_convert.assign_charset(map_charset->c_str(), target_charset))
            {
                g_logger.error("assign %s %to %s fail", map_charset->c_str(), target_charset);
                return false;
            }
        }
        return true;
    }

    void bus_event_t::print_info()
    {
        g_logger.error("update position info:[%s:%lu]", map_position.binlog_file, map_position.binlog_offset);
    }

    int32_t bus_event_t::parse_event(char *buf, uint32_t &pos, uint32_t packlen, bus_user_process
            *pUserProcess)
    {
        assert(packlen >= 19);
        this->pack_len = packlen;
        event_head.parse_event_head(buf, pos);
        int ret = -1;

        /*
         *@todo更新 binlog的position
         * rotate event 或者binlog中不存在的fileformate event,  包体中 logpos = 0，用它减去event_sz 会造成溢出
         */
        if (event_head.log_pos != 0)
        {
            uint32_t next_binlog_offset = event_head.log_pos;
            std::string &next_binlog_name = rotate_event.get_binlog_filename();
            map_position.set_position(next_binlog_name.c_str(), next_binlog_offset);
            g_logger.debug("type:%d, logfilename:%s, curpos:%lu, nextlogpos:%lu", event_head.event_type,
                    rotate_event.get_binlog_filename().c_str(), event_head.log_pos - packlen + 1, event_head.log_pos);


#if 0
            // 外网频繁写redis造成了很大的延迟，改为解到需要的数据才记录新的位置
            if (pUserProcess != NULL)
                pUserProcess->SaveNextreqPos(next_binlog_name.c_str(), next_binlog_offset);
#endif

        }

        switch(event_head.event_type)
        {
            case 0x00:
                g_logger.debug("unknown event");
                return -1;
                break;
            case 0x04:
#if 1
                if (m_bIsChecksumEnable == true && format_event.get_alg() != BINLOG_CHECKSUM_ALG_OFF && format_event.get_alg() != BINLOG_CHECKSUM_ALG_UNDEF)
                    this->pack_len -= BINLOG_CHECKSUM_LEN;    
#endif
                g_logger.debug("rotate event");

                if (rotate_event.parse_rotate_event_body(buf, pos, *this))
                {
                    g_logger.error("parse rotate event error");
                    return -1;
                }


                map_position.set_position(rotate_event.get_binlog_filename().c_str(), rotate_event.get_binlog_offset());

                break;
            case 0x0f:
                {
                    g_logger.debug("format description event");
#if 1
                    /*
                     *@todo 清掉一些内存，防止申请太多内存造成内存溢出
                     */
                    bus_table_map_t *pMapEvent = NULL;
                    std::map<uint64_t, bus_table_map_t *>::iterator it = map_map_event.begin();
                    while (it != map_map_event.end())
                    {
                        g_logger.debug("delete bus_table_map_t, table_id:%lu", it->first);
                        pMapEvent = it->second;   
                        if (pMapEvent != NULL)
                        {
                            delete pMapEvent;
                            pMapEvent = NULL;
                        }
                        map_map_event.erase(it++);
                    }
#endif

                    if(format_event.parse_format_event_body(buf, pos, *this, m_bIsChecksumEnable))
                    {
                        g_logger.error("parse format event error");
                        return -1;
                    }
                }
                break;
            case 0x13:
                {
#if 1
                    if (m_bIsChecksumEnable == true && format_event.get_alg() != BINLOG_CHECKSUM_ALG_OFF && format_event.get_alg() != BINLOG_CHECKSUM_ALG_UNDEF)
                        this->pack_len -= BINLOG_CHECKSUM_LEN;    
#endif

                    std::string &binlog_name = rotate_event.get_binlog_filename();
                    uint32_t offset = event_head.get_event_pos();
                    g_logger.debug("table map event, [%s:%lu]", binlog_name.c_str(), offset);

                    bus_table_map_t *pMapEvent = NULL;

                    uint8_t post_head_len = get_event_posthead_len(0x13);
                    uint64_t table_id = 0;
                    if (post_head_len == 8)
                    {
                        table_id = uint6korr(buf + pos);
                    } else {
                        table_id = uint4korr(buf + pos);
                    }
                    std::map<uint64_t, bus_table_map_t *>::iterator it = map_map_event.find(table_id);
                    if (it == map_map_event.end())
                    {
                        pMapEvent = new bus_table_map_t();
                        if (pMapEvent == NULL)
                        {
                            g_logger.error("pMapEvent == NULL, return");
                            return 0;
                        }

                        map_map_event[table_id] = pMapEvent;
                        g_logger.debug("new bus_table_map_t, table_id:%lu", table_id);
                    }
                    else
                    {
                        pMapEvent = map_map_event[table_id];
                    }
                    
                    if (pMapEvent->parse_map_event_body(buf, pos, *this))
                    {
                        g_logger.error("parse map event error");
                        return -1;
                    }
#if 0
                    if (!init_event_schema_charset())
                    {
                        g_logger.error("init map event charset fail");
                        return -1;
                    }
#endif
                }
                break;
            case 0x17:
                {
                    g_logger.debug("write rows event v1, [%s:%lu]", 
                            map_position.binlog_file, map_position.binlog_offset);
                    row_event.init(0x17, 1);
#if 1
                    if (m_bIsChecksumEnable == true && format_event.get_alg() != BINLOG_CHECKSUM_ALG_OFF && format_event.get_alg() != BINLOG_CHECKSUM_ALG_UNDEF)
                        this->pack_len -= BINLOG_CHECKSUM_LEN;
#endif
                    ret = row_event.parse_row_event_body(buf, pos, *this, pUserProcess);
                    if (ret == -1)
                    {
                        g_logger.error("parse write event fail");
                        return -1;
                    } else if (ret == 1)
                    {
                        g_logger.notice("write row event, recv stop signal");
                        return 1;
                    }
                }
                break;
            case 0x18:
                {
                    g_logger.debug("update rows event v1, [%s:%lu]", 
                            map_position.binlog_file, map_position.binlog_offset);
                    row_event.init(0x18, 1);
#if 1
                    if (m_bIsChecksumEnable == true && format_event.get_alg() != BINLOG_CHECKSUM_ALG_OFF && format_event.get_alg() != BINLOG_CHECKSUM_ALG_UNDEF)
                        this->pack_len -= BINLOG_CHECKSUM_LEN;
#endif

                    ret = row_event.parse_row_event_body(buf, pos, *this, pUserProcess);
                    if (ret == -1)
                    {
                        g_logger.error("parse write event fail");
                        return -1;
                    } else if (ret == 1)
                    {
                        g_logger.notice("update row  event, recv stop signal");
                        return 1;
                    }
                }
                break;
            case 0x19:
                {
                    g_logger.debug("delete rows event v1, [%s:%lu]", 
                            map_position.binlog_file, map_position.binlog_offset);
                    row_event.init(0x19, 1);
#if 1
                    if (m_bIsChecksumEnable == true && format_event.get_alg() != BINLOG_CHECKSUM_ALG_OFF && format_event.get_alg() != BINLOG_CHECKSUM_ALG_UNDEF)
                        this->pack_len -= BINLOG_CHECKSUM_LEN;
#endif
                    ret = row_event.parse_row_event_body(buf, pos, *this, pUserProcess);
                    if (ret == -1)
                    {
                        g_logger.error("parse delete event fail");
                        return -1;
                    } else if (ret == 1)
                    {
                        g_logger.notice("delete row event, recv stop signal");
                        return 1;
                    }
                }
                break;
            case 0x1E:
                {
                    g_logger.debug("write rows event v2, [%s:%lu]", 
                            map_position.binlog_file, map_position.binlog_offset);
                    row_event.init(0x1E, 2);
#if 1
                    if (m_bIsChecksumEnable == true && format_event.get_alg() != BINLOG_CHECKSUM_ALG_OFF && format_event.get_alg() != BINLOG_CHECKSUM_ALG_UNDEF)
                        this->pack_len -= BINLOG_CHECKSUM_LEN;
#endif

                    ret = row_event.parse_row_event_body(buf, pos, *this, pUserProcess);
                    if (ret == -1)
                    {
                        g_logger.error("parse write event fail");
                        return -1;
                    } else if (ret == 1)
                    {
                        g_logger.notice("write row event, recv stop signal");
                        return 1;
                    }
                }
                break;
            case 0x1F:
                {
                    g_logger.debug("update rows event v2, [%s:%lu]", 
                            map_position.binlog_file, map_position.binlog_offset);
                    row_event.init(0x1F, 2);

#if 1
                    if (m_bIsChecksumEnable == true && format_event.get_alg() != BINLOG_CHECKSUM_ALG_OFF && format_event.get_alg() != BINLOG_CHECKSUM_ALG_UNDEF)
                        this->pack_len -= BINLOG_CHECKSUM_LEN;    
#endif

                    ret = row_event.parse_row_event_body(buf, pos, *this, pUserProcess);
                    if (ret == -1)
                    {
                        g_logger.error("parse write event fail");
                        return -1;
                    } else if (ret == 1)
                    {
                        g_logger.notice("update row  event, recv stop signal");
                        return 1;
                    }
                }
                break;
            case 0x20:
                {
                    g_logger.debug("delete rows event v2, [%s:%lu]", map_position.binlog_file, map_position.binlog_offset);
                    row_event.init(0x20, 2);

#if 1
                    if (m_bIsChecksumEnable == true && format_event.get_alg() != BINLOG_CHECKSUM_ALG_OFF && format_event.get_alg() != BINLOG_CHECKSUM_ALG_UNDEF)
                        this->pack_len -= BINLOG_CHECKSUM_LEN;    
#endif

                    ret = row_event.parse_row_event_body(buf, pos, *this, pUserProcess);
                    if (ret == -1)
                    {
                        g_logger.error("parse delete event fail");
                        return -1;
                    } else if (ret == 1)
                    {
                        g_logger.notice("delete row event, recv stop signal");
                        return 1;
                    }
                }
                break;

            default:
                pos = packlen;
        }
        return 0;
    }
    /***********************************************************/
    int32_t bus_format_event_t::parse_format_event_body(char *buf, 
            uint32_t &pos, bus_event_t &cur_event, bool isChecksumEnable)
    {
        _binlog_ver = uint2korr(buf + pos);
        pos += 2;

        uint32_t sz = sizeof(_serv_ver) - 1;
        strncpy(_serv_ver, buf + pos, sz);
        _serv_ver[sz] = '\0';
        pos += sz;

        _time_stamp = uint4korr(buf + pos);
        pos += 4;

        _head_len = buf[pos];
        pos += 1;

        int left = 0;
        if (isChecksumEnable == true){
            left = cur_event.pack_len - pos - BINLOG_CHECKSUM_LEN - BINLOG_CHECKSUM_ALG_DESC_LEN;
            _post_head_len_str.assign(buf + pos, left);
            pos += left;

            _alg = * (uint8*) (buf + cur_event.pack_len - BINLOG_CHECKSUM_LEN - BINLOG_CHECKSUM_ALG_DESC_LEN);
            pos += BINLOG_CHECKSUM_ALG_DESC_LEN;
            pos += BINLOG_CHECKSUM_LEN;
        }
        else
        {
            left = cur_event.pack_len - pos;
            _post_head_len_str.assign(buf + pos, left);
            pos += left;
        }

        g_logger.debug("_binlog_ver:%d, server_ver:%s", _binlog_ver, _serv_ver);

        return 0;
    }
    /***********************************************************/
    int32_t bus_rotate_event_t::parse_rotate_event_body(char *buf,
            uint32_t &pos, bus_event_t &cur_event)
    {
        _next_pos = uint8korr(buf + pos);
        pos += 8;
        int left = cur_event.pack_len - pos;
        _next_binlog_filename.assign(buf + pos, left);
        pos += left;
        return 0;
    }
    /***********************************************************/
    int32_t bus_table_map_t::parse_map_event_body(char *buf, uint32_t &pos, bus_event_t &cur_event)
    {
        uint8_t post_head_len = cur_event.get_event_posthead_len(_cmd);
        if (post_head_len == 8)
        {
            _table_id = uint6korr(buf + pos);
            pos += 6;
        } else {
            _table_id = uint4korr(buf + pos);
            pos += 4;
        }

        g_logger.debug("parse_map_event_body, table_id:%lu", _table_id);
        _flags = uint2korr(buf + pos);
        pos += 2;

        _database_name_length = buf[pos];
        pos += 1;
        _database_name.assign(buf + pos, _database_name_length);
        pos += _database_name_length;
        pos += 1;

        _table_name_length = buf[pos];
        pos +=1;
        _table_name.assign(buf + pos, _table_name_length);
        pos += _table_name_length;
        pos += 1;

        if(!unpack_int(buf + pos, pos, _column_count))
        {
            g_logger.error("read column count fail");
            return -1;
        }
        _column_type_def.assign(buf + pos, _column_count);
        pos += _column_count;
        //读取column_meta
        if (!unpack_int(buf + pos, pos, _column_meta_length))
        {
            g_logger.error("read column meta length fail");
            return -1;
        }
        _column_meta_def.assign(buf + pos, _column_meta_length);
        pos += _column_meta_length;

        //解析column_meta
        _meta_vec.clear();
        uint32_t meta_pos = 0;
        for (uint32_t i = 0; i < _column_count; ++i)
        {
            uint16_t meta = read_meta(_column_meta_def, meta_pos, (uint8_t)_column_type_def.at(i));
            _meta_vec.push_back(meta);
        }
        assert(meta_pos == _column_meta_length);

        uint64_t null_bitmap_length = (_column_count + 7) >> 3;
        _null_bitmap.assign(buf + pos, null_bitmap_length);
        pos += null_bitmap_length;

        return 0;
    }
    /***********************************************************/

    int32_t bus_row_event_t::parse_row_event_body(char *buf, uint32_t &pos, bus_event_t &cur_event,
            bus_user_process *pUserProcess)
    {

        uint8_t post_head_len = cur_event.get_event_posthead_len(_cmd);
        if (post_head_len == 6)
        {
            _table_id = uint4korr(buf + pos);
            pos += 4;
        } else {
            _table_id = uint6korr(buf + pos);
            pos += 6;
        }

        g_logger.debug("_table_id = %lu", _table_id);
        if (cur_event.map_map_event.find(_table_id) == cur_event.map_map_event.end())
        {
            g_logger.error("no tablemapevent for this rowevent");
            return -1; 
        }
        bus_table_map_t *pMapEvent = cur_event.map_map_event[_table_id];
        if (pMapEvent == NULL)
        {
            g_logger.error("pMapEvent is NULL");
            return -1;
        }

        if (!cur_event.init_event_schema_charset(pMapEvent))
        {
            g_logger.error("init map event charset fail");
            return -1;
        }

        g_logger.debug("[%s.%s] %x", pMapEvent->get_database_name().c_str(), pMapEvent->get_table_name().c_str(), _cmd);

        if (_table_id != pMapEvent->get_tableid())
        {
            g_logger.error("_table_id:%lu, pMapEvent->get_tableid():%lu", _table_id, pMapEvent->get_tableid());
            return 0;
        }

        _flags = uint2korr(buf + pos);
        pos += 2;

#if 1
        if (_ver == 2)
        {
            _extra_data_length = uint2korr(buf + pos);
            pos += _extra_data_length;
        }
#endif

        if(!unpack_int(buf + pos, pos, _column_count))
        {
            g_logger.error("parse column count fail");
            return -1;
        }

        uint64_t _present_bitmap_len = (_column_count + 7) >> 3;
        //读取present bitmap
        _present_bitmap1.assign(buf + pos, _present_bitmap_len);
        pos += _present_bitmap_len;
        //解析present bitmap
        uint32_t present_ct1 = _present_bitmap1.get_bitset_count(_column_count);
        uint32_t null_bitmap1_size = (present_ct1 + 7) >> 3;

        uint32_t null_bitmap2_size = 0;
        if (_cmd == 0x18 || _cmd == 0x1F)
        {
            _present_bitmap2.assign(buf + pos, _present_bitmap_len);
            pos += _present_bitmap_len;
            uint32_t present_ct2 = _present_bitmap2.get_bitset_count(_column_count);
            null_bitmap2_size = (present_ct2 + 7) >> 3;
        }

#if 1
        schema_t *match_schema = cur_event.map_schema;
        if (match_schema == NULL)
        {
            g_logger.debug("[%s.%s] don't match, ignore row",
                    pMapEvent->get_database_name().c_str(),
                    pMapEvent->get_table_name().c_str());
            pos =  cur_event.pack_len;
            return 0;
        }


        std::string *pcharset = cur_event.map_charset;
        if (pcharset == NULL)
        {
            g_logger.error("[%s.%s] get charset fail",
                    pMapEvent->get_database_name().c_str(),
                    pMapEvent->get_table_name().c_str());

            return -1;
        }
#endif

#if 1
        // 外网频繁写redis造成了很大的延迟，改为解到需要的数据才记录新的位置
        if (pUserProcess != NULL)
        {
            uint32_t next_binlog_offset = cur_event.event_head.log_pos;
            std::string &next_binlog_name = cur_event.rotate_event.get_binlog_filename();

            pUserProcess->SaveNextreqPos(next_binlog_name.c_str(), next_binlog_offset);
            g_logger.notice("save binlog_pos to redis. binlog_filename:%s, binlog_pos:%ld", next_binlog_name.c_str(), next_binlog_offset);
        }
#endif
        while (cur_event.pack_len > pos)
        {
            row_t *row = new(std::nothrow)row_t(_column_count);
            if (row == NULL) oom_error();
            row->set_table(pMapEvent->get_table_name().c_str());
            row->set_db(pMapEvent->get_database_name().c_str());

            if (_cmd == 0x17 || _cmd == 0x1E) {
                row->set_action(INSERT);
                if (!parse_row(buf, pos, null_bitmap1_size, row, _present_bitmap1, false, cur_event,
                            *pMapEvent))
                {
                    g_logger.error("parse [%s:%lu] row fail",
                            cur_event.map_position.binlog_file,
                            cur_event.map_position.binlog_offset);

                    if (row)
                    {
                        delete row;
                        row = NULL;
                    }
                    return -1;
                }
            } else if (_cmd == 0x19 || _cmd == 0x20) {
                row->set_action(DEL);
                if (!parse_row(buf, pos, null_bitmap1_size, row, _present_bitmap1, false, cur_event,
                            *pMapEvent))
                {
                    g_logger.error("parse [%s:%lu] row fail",
                            cur_event.map_position.binlog_file,
                            cur_event.map_position.binlog_offset);

                    if (row)
                    {
                        delete row;
                        row = NULL;
                    }
                    return -1;
                }

            }else if(_cmd == 0x18 || _cmd == 0x1F) {
                row->set_action(UPDATE);
                if (!parse_row(buf, pos, null_bitmap1_size, row, _present_bitmap1, true, cur_event,
                            *pMapEvent))
                {
                    g_logger.error("parse [%s:%lu] row fail",
                            cur_event.map_position.binlog_file,
                            cur_event.map_position.binlog_offset);
                    if (row)
                    {
                        delete row;
                        row = NULL;
                    }
                    return -1;
                }

                if (!parse_row(buf, pos, null_bitmap2_size, row, _present_bitmap2, false, cur_event,
                            *pMapEvent)) {
                    g_logger.error("parse [%s:%lu] row fail",
                            cur_event.map_position.binlog_file,
                            cur_event.map_position.binlog_offset);
                    if (row)
                    {
                        delete row;
                        row = NULL;
                    }
                    return -1;
                }
            } else {
                g_logger.error("unsupport command=%hhu", _cmd);
                if (row)
                {
                    delete row;
                    row = NULL;
                }
                return -1;
            }

            if (pUserProcess)
                pUserProcess->IncrProcess(row);
#if 0
            /*
               typedef struct 
               {
               char        szNewRowMem[1024];      //[out] 存储各列值的内存块
               char        szOldRowMem[1024];      //[out] 存储各列值的内存块,仅仅用于update事件，存储更新前旧的行数据
               int         nColumnCount;           //[in]  我们需要的列数
               int         nMaxValueLen;           //[in]  每一列长度
               action_t    action;                 //[in]  操作类型
               char        szDatabaseName[64];     //[out] 数据库名
               char        szTableName[64];        //[out] 表名
               char        szBinlogFileName[64];   //[out] 目前binlog文件名
               uint32_t    uBinlogPos;             //[out] 目前binlog的位置，偏移量
               }cb_process_param_t;
             */

            if (pInterface->m_cb_process != NULL && pInterface->m_cb_process_param != NULL)
            {
                pInterface->m_cb_process_param->nColumnCount = row->size();
                if (_cmd == 0x17 || _cmd == 0x1E)
                    pInterface->m_cb_process_param->action = INSERT;
                else if (_cmd == 0x18 || _cmd == 0x1F)
                    pInterface->m_cb_process_param->action = UPDATE;
                else if (_cmd == 0x19 || _cmd == 0x20)
                    pInterface->m_cb_process_param->action = DEL;

                strncpy(pInterface->m_cb_process_param->szDatabaseName, pMapEvent->get_database_name().c_str(),
                        sizeof(pInterface->m_cb_process_param->szDatabaseName));
                strncpy(pInterface->m_cb_process_param->szTableName, pMapEvent->get_table_name().c_str(),
                        sizeof(pInterface->m_cb_process_param->szTableName));
                strncpy(pInterface->m_cb_process_param->szBinlogFileName, cur_event.map_position.binlog_file,
                        sizeof(pInterface->m_cb_process_param->szBinlogFileName));
                pInterface->m_cb_process_param->uBinlogPos = cur_event.map_position.binlog_offset;
                char *pNewValue = NULL;
                char *pOldValue = NULL;
                char *p = NULL;
                for(uint32_t i = 0;i < row->size();i++)
                {
                    row->get_value(i,&p);
                    if (p == NULL)
                    {
                        pNewValue = pInterface->m_cb_process_param->szNewRowMem + i * pInterface->m_cb_process_param->nMaxValueLen;
                        memset(pNewValue, 0, pInterface->m_cb_process_param->nMaxValueLen);
                        continue;
                    }

                    pNewValue = pInterface->m_cb_process_param->szNewRowMem + i * pInterface->m_cb_process_param->nMaxValueLen;
                    strncpy(pNewValue, p, pInterface->m_cb_process_param->nMaxValueLen);
                    g_logger.notice("action:%d, database:%s, table:%s, binlogFile:%s, binlogPos:%d, new, index:%d, value:%s ", 
                            pInterface->m_cb_process_param->action, 
                            pInterface->m_cb_process_param->szDatabaseName,
                            pInterface->m_cb_process_param->szTableName, 
                            pInterface->m_cb_process_param->szBinlogFileName,
                            pInterface->m_cb_process_param->uBinlogPos, 
                            i, 
                            pNewValue);

                    if (_cmd == 0x18 || _cmd == 0x1F)
                    {
                        row->get_old_value(i, &p);
                        if (p == NULL)
                        {
                            pOldValue = pInterface->m_cb_process_param->szOldRowMem + i * pInterface->m_cb_process_param->nMaxValueLen;
                            memset(pOldValue, 0, pInterface->m_cb_process_param->nMaxValueLen);
                            continue;
                        }

                        pOldValue = pInterface->m_cb_process_param->szOldRowMem + i * pInterface->m_cb_process_param->nMaxValueLen;
                        strncpy(pOldValue, p, pInterface->m_cb_process_param->nMaxValueLen);
                        g_logger.notice("action:%d, database:%s, table:%s, binlogFile:%s, binlogPos:%d, old, index:%d, value:%s ", 
                                pInterface->m_cb_process_param->action, 
                                pInterface->m_cb_process_param->szDatabaseName,
                                pInterface->m_cb_process_param->szTableName, 
                                pInterface->m_cb_process_param->szBinlogFileName,
                                pInterface->m_cb_process_param->uBinlogPos, 
                                i, 
                                p);
                    }
                }

                pInterface->m_cb_process(pInterface->m_cb_process_param);
            }
#endif
            if (row)
            {
                delete row;
                row = NULL;
            }
        }

        return 0;
    }


    bool bus_row_event_t::parse_row(char *buf,
            uint32_t &pos,
            uint32_t null_bitmap_size,
            row_t *row,
            bus_mem_block_t &present_bitmap,
            bool is_old,
            bus_event_t &cur_event,
            bus_table_map_t &map)
    {
        //bus_table_map_t& map = cur_event.map_event;
        convert_charset_t& convert = cur_event.map_convert;

        _null_bitmap.assign(buf + pos, null_bitmap_size);
        pos += null_bitmap_size;
        /* 构造临时行数据结构，存储解析后的数据 */
        row_t *temp_row = new(std::nothrow) row_t(_column_count);
        if (temp_row == NULL) oom_error();

        schema_t *curschema = cur_event.map_schema;
        uint32_t present_pos = 0;
        for (uint32_t i = 0; i < _column_count; ++i)
        {
            if (present_bitmap.get_bit(i)) {
                bool null_col = _null_bitmap.get_bit(present_pos);
                if (null_col) {
                    temp_row->push_back(NULL, false);
                } else {
                    uint8_t type = map.get_column_type(i);
                    uint16_t meta = map.get_column_meta(i);
                    column_t *col = curschema->get_column_byseq(i);
                    //column_t *col = NULL;
                    if (!parse_column_value(buf + pos, type, meta, pos, temp_row, false, convert, col))
                    {
                        g_logger.error("parse %d column value fail", i);
                        delete temp_row;
                        temp_row = NULL;
                        return false;
                    }
                }
                ++present_pos;
            } else {
                temp_row->push_back(NULL, false);
            }
        }
#if 1

        /* 将解析后的数据按照顺序放入目标行 */
        std::vector<column_t*>& cols = curschema->get_columns();
        char *ptr;
        for(std::vector<column_t*>::iterator it = cols.begin(); it != cols.end(); ++it)
        {
            column_t *curcolumn = *it;
            int32_t curseq = curcolumn->get_column_seq();
            if (!temp_row->get_value(curseq, &ptr))
            {
                g_logger.error("get value in sequence %d fail", curseq);
                delete temp_row;
                temp_row = NULL;
                return false;
            }
            row->push_back(ptr, is_old);
        }
#endif

        /* 释放临时行数据结构占用的内存 */
        if (temp_row != NULL)
        {
            delete temp_row;
            temp_row = NULL;
        }
        return true;
    }

}//namespace bus
/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
