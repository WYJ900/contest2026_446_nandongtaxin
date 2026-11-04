package com.velaride.companion;

import java.io.IOException;
import java.util.Arrays;
import java.util.zip.CRC32;

public final class VoiceMemoTest {
    static void check(boolean value){if(!value)throw new AssertionError();}
    public static void main(String[] args)throws Exception {
        byte[] wav=new byte[96060];
        for(int i=0;i<wav.length;i++)wav[i]=(byte)i;
        wav[0]='R';wav[1]='I';wav[2]='F';wav[3]='F';wav[8]='W';wav[9]='A';wav[10]='V';wav[11]='E';
        CRC32 crc=new CRC32();crc.update(wav);
        VoiceMemo.Info info=new VoiceMemo.Info(1,12,wav.length,crc.getValue(),true);
        byte[] received=VoiceMemo.download(info,(offset,length)->Arrays.copyOfRange(wav,(int)offset,(int)offset+length));
        check(Arrays.equals(received,wav));
        try{VoiceMemo.download(info,(offset,length)->new byte[length]);throw new AssertionError();}catch(IOException expected){}
        try{VoiceMemo.download(info,(offset,length)->new byte[length-1]);throw new AssertionError();}catch(IOException expected){}
        try{VoiceMemo.download(info,(offset,length)->{throw new IOException("disconnected");});throw new AssertionError();}catch(IOException expected){}
        check(Arrays.equals(VoiceMemo.hex("0080ff",3),new byte[]{0,(byte)128,(byte)255}));
        for(String bad:new String[]{"0","xx","00ff"}) {
            try{VoiceMemo.hex(bad,1);throw new AssertionError();}catch(IOException expected){}
        }
        try{new VoiceMemo.Info(1,12,96000,0,true);throw new AssertionError();}catch(IllegalArgumentException expected){}
        System.out.println("PASS: bounded WAV transfer, exact offsets/lengths, CRC rejection, disconnect, strict hex and metadata bounds");
    }
}
