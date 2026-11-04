package com.velaride.companion;

import java.io.*;
import java.util.zip.CRC32;

/** Bounded 3-second watch WAV transfer. No recording or playback side effects. */
public final class VoiceMemo {
    public static final class Info {
        public final long id,sequence,bytes,crc;
        public final boolean pending;
        public Info(long id,long sequence,long bytes,long crc,boolean pending) {
            if(id<1||id>9999999||sequence<0||sequence>0xffffffffL||bytes!=96060||crc<0||crc>0xffffffffL)
                throw new IllegalArgumentException("语音文件信息异常");
            this.id=id;this.sequence=sequence;this.bytes=bytes;this.crc=crc;this.pending=pending;
        }
    }
    public interface Source { byte[] read(long offset,int length) throws Exception; }
    public static byte[] download(Info info,Source source) throws Exception {
        ByteArrayOutputStream bytes=new ByteArrayOutputStream((int)info.bytes);
        while(bytes.size()<info.bytes) {
            int length=(int)Math.min(160,info.bytes-bytes.size());
            byte[] chunk=source.read(bytes.size(),length);
            if(chunk==null||chunk.length!=length)throw new IOException("语音分片长度异常，请重新同步");
            bytes.write(chunk);
        }
        byte[] data=bytes.toByteArray();CRC32 crc=new CRC32();crc.update(data);
        if(crc.getValue()!=info.crc)throw new IOException("语音校验失败，文件未保存，请重试");
        if(data[0]!='R'||data[1]!='I'||data[2]!='F'||data[3]!='F'||data[8]!='W'||data[9]!='A'||data[10]!='V'||data[11]!='E')
            throw new IOException("手表语音不是有效 WAV 文件");
        return data;
    }
    public static byte[] hex(String value,int length) throws IOException {
        if(value.length()!=length*2)throw new IOException("语音分片长度异常");
        byte[] bytes=new byte[length];
        for(int i=0;i<length;i++) {
            int high=Character.digit(value.charAt(i*2),16),low=Character.digit(value.charAt(i*2+1),16);
            if(high<0||low<0)throw new IOException("语音分片编码异常");
            bytes[i]=(byte)(high*16+low);
        }
        return bytes;
    }
    private VoiceMemo(){}
}
