/*
 * Copyright (C) 2014
 *
 * Author: Juyoung Ryu <ujuozz@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wbtypes.h"
#include "wbio.h"
#include "wblib.h"
#include "nvtfat.h"
#include "W55FA93_reg.h"
#include "usb.h"
#include "header_info.h"

#define printf  sysprintf
#define UMAS_DBG 0
#define get_timer_ticks()   sysGetTicks(TIMER0)

#if UMAS_DBG
#define UMAS_dbg                sysprintf

#else
#define UMAS_dbg(...)           do {} while (0)

#endif


// adler32 does not use library routine 
#define BASE 65521L /* largest prime smaller than 65536 */
#define NMAX 5552
/* NMAX is the largest n such that 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 */
#define DO1(buf,i)  {s1 += buf[i]; s2 += s1;}
#define DO2(buf,i)  DO1(buf,i); DO1(buf,i+1);
#define DO4(buf,i)  DO2(buf,i); DO2(buf,i+2);
#define DO8(buf,i)  DO4(buf,i); DO4(buf,i+4);
#define DO16(buf)   DO8(buf,0); DO8(buf,8);

//massstorage 
#define FAT16   16
#define FAT32   32
#define FILE_NAMESIZE 11


UINT8 st_partition[4];
int reserved;
int secperfat;
int firstroot;
int secperclu;
int fatno;
char boot_buf[1024];
char buf[1024];

typedef struct
{
    unsigned long BytePerSec;   
    unsigned char SecPerClus;
    unsigned long reserved;
    unsigned char NumberOfFat;
    unsigned int  NumberOfSec;
    unsigned int  SecPerFAT;
    unsigned char TypeOfFAT;
    unsigned int  FirstClusOfRoot; 
    unsigned long MaxentryOfRoot; //fat16
    unsigned int  NumberOfEntry;     //Number of Entry in cluster
    unsigned int  CntOfEntry:1;     

    unsigned long LBAOfFirstSec;  //LBA of first sector in volumn.
    unsigned long LBAOfFat;       //LBA of FAT in volumn.
    unsigned long LBAOfRootDir;   //LBA of root directory in volumn.
    unsigned long LBAOfDataArea;  //LBA of data area in volumn.
    unsigned long MaxClus;        //maximum cluster number in volumn.
} DISKINFO;
DISKINFO pdiskinfo[4];

typedef struct
{
    unsigned int FirstClus; //first cluster number in file
    unsigned int CurrentClus; //current cluster number
    unsigned int CurrentSec;  //current sector in current cluster
    unsigned int CurrentPos;  //current position in sector.
    unsigned long FileSize;   //file size
} FILEINFO;
typedef FILEINFO* FILEOBJ;
FILEOBJ fo;



typedef struct __DIRENTRY
{
    unsigned short  DIR_FstClusHI;
    unsigned short  DIR_FstClusLO;
    unsigned long DIR_FileSize;
}_DIRENTRY;

typedef _DIRENTRY* DIRENTRY;
DIRENTRY en;
/*
typedef struct _n3290x_sys_header {
    unsigned long boot_marker;
    unsigned long start_addr;
    unsigned long length;
    unsigned long DQS_marker;
    unsigned long dqsods;
    unsigned long ckdqsds;
    unsigned long dramsize_marker;
    unsigned long dram_size;
} n3290x_sys_header_t;
*/

typedef struct header
{
    unsigned long marker;
    unsigned long size;
    unsigned long version;
    unsigned long adler32;
} header_t;

typedef header_t* HEAD16;
HEAD16 h2;



unsigned long adler32(unsigned long adler, unsigned char *buf, unsigned short int len)
{
    unsigned s1 = adler & 0xffff;
    unsigned s2 = (adler >> 16) & 0xffff;
    short int k;
  

    if (buf == NULL) return 1L;

    while (len > 0) 
    {
        k = len < NMAX ? len : NMAX;
        len -= k;
        while (k >= 16) 
        {
            DO16(buf);

            buf += 16;
            k -= 16;
//            S.sprintf("k=%ld, buf= %x",k, buf);
//            Memo1->Lines->Add(S);
        }
        if (k != 0) 
        {
            do 
            {
                s1 += *buf++;
                s2 += s1;
//            S.sprintf("1:s1=%lx, s2=%lx",s1,s2);
//            Memo1->Lines->Add(S);
            } while (--k);
            s1 %= BASE;
            s2 %= BASE;
//        S.sprintf("2:s1=%lx, s2=%lx",s1,s2);
//        Memo1->Lines->Add(S);
        }
    }
    return (s2 << 16) | s1;

}

int find_partition(PDISK_T* pdisk)
{
    int ret, i;
    int entry[4]={446,462,478,494};
    int offset = 8;

    
    ret = umas_disk_read(pdisk, 0, 2, buf);

    for(i=0; i<sizeof(buf); i++)
    {
     
       if((i%32) == 0)
       {
          UMAS_dbg("%d\n",i);
       }
       UMAS_dbg("0x%X ",buf[i]);
    }

    if(ret)
    {
        UMAS_dbg("error, partition\n");
    }
    else
    {
        int i,value;
        for(i=0; i<4; i++)
        {
            value = entry[i]+offset;

            st_partition[i] = buf[value];
            
            UMAS_dbg("start_%d partition:%d\n", i,st_partition[i]);
        }
    }

    return 0;
}



int get_bpb_info(PDISK_T* pdisk,DISKINFO* dsk, int partition)
{
 //   char boot_buf[512]={0};
 //   char buf[512]={0};
       
    int buf1=0;
    int ret = -1;

    
//    memset((void*)buf, 0, sizeof(buf));
/*  int boot_backup=0;

    boot_backup = buf[50];
    boot_backup |= buf[51]<<8;

    if(boot_backup)
        ret = boot_backup;

    else 
        ret = 0;
*/      

    memset((void*)boot_buf, 0, sizeof(boot_buf));

    umas_disk_read(pdisk,partition,1, boot_buf); //boot sector info

    if(boot_buf[22])   //FAT32일 경우 0, FAT16일 경우 FAT당 섹터 수
        dsk->TypeOfFAT = FAT16;
    else
        dsk->TypeOfFAT = FAT32;


    buf1 = boot_buf[11];
    buf1 |= (boot_buf[12]<<8);
    dsk->BytePerSec = (unsigned long)buf1;
    UMAS_dbg("bytes per sector : %d\n", dsk->BytePerSec);
    if(dsk->BytePerSec == 0)
        return ret;

    
    dsk->SecPerClus = boot_buf[13];
    UMAS_dbg("sector per cluster : %d\n", dsk->SecPerClus);
    if(dsk->SecPerClus == 0)
        return ret;


    buf1 = boot_buf[14];
    buf1 |= (boot_buf[15]<<8);
    dsk->reserved = buf1;
    UMAS_dbg("sector of reserved\n", dsk->reserved);
    

    dsk->NumberOfFat = boot_buf[16];
    UMAS_dbg("fat no. : %d\n", dsk->NumberOfFat);
    if(dsk->NumberOfFat == 0)
        return ret;

    
    buf1 = boot_buf[32];
    buf1 |= (boot_buf[33]<<8);
    buf1 |= (boot_buf[34]<<16);
    buf1 |= (boot_buf[35]<<24);
    dsk->NumberOfSec = buf1;
    UMAS_dbg("sector no. : %d\n", dsk->NumberOfSec);


    if(dsk->TypeOfFAT == FAT32)
    {
        buf1 = boot_buf[36];
        buf1 |= (boot_buf[37]<<8);
        buf1 |= (boot_buf[38]<<16);
        buf1 |= (boot_buf[39]<<24);
        dsk->SecPerFAT = buf1;
        UMAS_dbg("sectors per fat : %d\n", dsk->SecPerFAT);
    }
    else if(dsk->TypeOfFAT == FAT16)
    {
        buf1 = boot_buf[22];
        buf1 |= (boot_buf[23]<<8);
        dsk->SecPerFAT = buf1;
        UMAS_dbg("sectors per fat : %d\n", dsk->SecPerFAT);
    }
 
    
    if(dsk->TypeOfFAT == FAT32)
    {
        buf1 = boot_buf[44];
        buf1 |= (boot_buf[45]<<8);
        buf1 |= (boot_buf[46]<<16);
        buf1 |= (boot_buf[47]<<24);
        dsk->FirstClusOfRoot = buf1;
        UMAS_dbg("0x%X, 0x%X, 0x%X, 0x%X\n",boot_buf[44],boot_buf[45],boot_buf[46],boot_buf[47]);
        UMAS_dbg("first root cluster : %d\n", dsk->FirstClusOfRoot);
    }
    if(dsk->TypeOfFAT == FAT16)
    {
        buf1 = boot_buf[17];
        buf1 |= (boot_buf[18]<<8);
        dsk->MaxentryOfRoot = buf1;
        UMAS_dbg("max entry of root : %d\n", dsk->MaxentryOfRoot);
    }


    dsk->NumberOfEntry = (dsk->SecPerClus*dsk->BytePerSec/((dsk->TypeOfFAT)/sizeof(char)));

    dsk->LBAOfFirstSec = partition;                   
    UMAS_dbg("LBAOfFirstSec : %d\n",dsk->LBAOfFirstSec);

    dsk->LBAOfFat = partition+(dsk->reserved);        
    UMAS_dbg("LBAOfFat : %d\n",dsk->LBAOfFat);

    dsk->LBAOfDataArea = partition+(dsk->reserved)+(dsk->SecPerFAT*dsk->NumberOfFat);
    UMAS_dbg("LBAOfDataArea : %d\n",dsk->LBAOfDataArea);

    if(dsk->TypeOfFAT == FAT32)
    {
        if(dsk->FirstClusOfRoot >=2)
        {
         dsk->LBAOfRootDir = partition+(dsk->reserved)+(dsk->SecPerFAT*dsk->NumberOfFat)+((dsk->FirstClusOfRoot-2)*dsk->SecPerClus);   
         UMAS_dbg("root dir : %d\n",dsk->LBAOfRootDir);
        }
        else
        {
         UMAS_dbg("Error, First Cluster Number %d\n", dsk->FirstClusOfRoot-2);
         return -1;
        }
    }
    else if(dsk->TypeOfFAT == FAT16)
    {
        dsk->LBAOfRootDir = dsk->LBAOfDataArea;
        UMAS_dbg("root dir : %d\n",dsk->LBAOfRootDir);
    }

    dsk->MaxClus = (dsk->NumberOfSec-(dsk->reserved + (dsk->SecPerFAT*dsk->NumberOfFat)))/dsk->SecPerClus ;
    UMAS_dbg("max cluster : %d\n",dsk->MaxClus);

/*  buf1 = buf[48];
    buf1 |= (buf[49]<<8);
    sysprintf("fsinfo : %d\n", buf1);   

*/

    return 0;//boot_backup;
}

int check_file(void)
{
 
    unsigned long adl=0;
    unsigned long marker=0;
    unsigned long version=0;
    unsigned char fail=0;

    unsigned long adl32=0;
    unsigned long rcv_adl32=0;

    h2->marker = 0x53545259;
    h2->version = REVISION_CNT;

    marker = (file_buf[0]);
    marker |= (file_buf[1]<<8);
    marker |= (file_buf[2]<<16);
    marker |= (file_buf[3]<<24);

    version = (file_buf[8]);
    version |= (file_buf[9]<<8);
    version |= (file_buf[10]<<16);
    version |= (file_buf[11]<<24);

    adl = (file_buf[12]);
    adl |= (file_buf[13]<<8);
    adl |= (file_buf[14]<<16);
    adl |= (file_buf[15]<<24);

    h2->adler32 = adler32(1,&file_buf[HEADER_LENGTH],f_size-HEADER_LENGTH);

    sysprintf("marker 0x%X, version %d, adl 0x%X\n",marker, version,adl);
    sysprintf("marker1 0x%X, version %d, adl 0x%X\n",h2->marker,h2->version,h2->adler32);
    if(marker == h2->marker)
    {
        sysprintf("Marker OK\n"); 
        if(version > (unsigned long)REVISION_CNT)
        {
            sysprintf("Firmware version OK\n");   
            if(adl == h2->adler32)
            {
                sysprintf("adler32 OK\n");

            }
            else
            {
                fail = 1;
                sysprintf("adler32 %d %d\n",adl, h2->adler32);   
            }
        }
        else
        {
            fail = 2;
            sysprintf("firmware version %d %d\n",version, h2->version);  
        }
    }
    else
    {
        fail = 3;
        sysprintf("marker 0x%X 0x%X\n",marker, h2->marker);  
    }



    return fail;
}

int find_file(PDISK_T *pdisk, DISKINFO *dsk)
{
    int ret = 0;
    int ret1 = 0;


    ret = find_file_name(pdisk, dsk);
    if(ret)
    {
        UMAS_dbg("no file\n");
        ret |= 0x1;
    }
    else
    {
        get_entry_info(pdisk, dsk);

        ret1 = read_file1(pdisk, dsk);

        if(ret1 == 4)
        {
            ret |=0x10000; 
        }
        ret1 = check_file();
        if(ret1)
        {
            if(ret1 == 1)
            {
                UMAS_dbg("adler32 FAIL\n");
                ret |=0x10; 
            }
            else if(ret1 == 2)
            {
                UMAS_dbg("version FAIL \n");
                ret |=0x100; 
            }
            else if(ret1 == 3)
            {
                UMAS_dbg("marker FAIL\n");
                ret |=0x1000; 
            }
        }

    }
    return ret;
}



int find_file_name(PDISK_T *pdisk, DISKINFO *dsk)
{
    int i=0;
    int j=0;
    int cnt=0;
    int ret=0;
    
    
    fo->CurrentClus = dsk->FirstClusOfRoot;
    fo->CurrentSec = dsk->LBAOfRootDir;
    
    do
    {
       
        for(cnt=0; cnt<dsk->SecPerClus; cnt++,fo->CurrentSec++)
        {
            memset((void*)f_buf, 0, sizeof(f_buf));
            j=0;
            UMAS_dbg("current sec %d, %d\n",fo->CurrentSec,dsk->LBAOfRootDir);
            
            umas_disk_read(pdisk,fo->CurrentSec, 1, f_buf);

            //memcpy(f_buf, &fat_buf[0], 512);
            memcpy(f_buf, &fat_buf[0], dsk->BytePerSec);

            for(i=0; i<dsk->BytePerSec/dsk->TypeOfFAT; i++)
            {
                UMAS_dbg("max i:%d j:%d 0x%X\n", dsk->BytePerSec/dsk->TypeOfFAT,j,f_buf[j]);
                if((f_buf[j] != 0x00) && (f_buf[j] != 0xE5))
                {
                    if(!strncmp(filename,&f_buf[j],11))
                    {  
                        UMAS_dbg("get file %d. %s\n",i,&f_buf[j]);
                        fo->CurrentPos = j;
                        return 0;  //CurrentSec,CurrentClus
                    }
                    else
                    {    
                        UMAS_dbg("no search file %s\n", &f_buf[j]);
                    }
                }
                j+=dsk->TypeOfFAT;
            }
        }
        ret = get_next_cluster(pdisk, dsk);
    //  fo->CurrentSec = (dsk->LBAOfRootDir) + ((fo->CurrentClus)*dsk->SecPerClus);
        
    }while(!ret);

    return 1;
}



int get_entry_info(PDISK_T *pdisk, DISKINFO *dsk)
{
    int filesize=0;
    char entry[32];

    memset((void*)entry, 0, sizeof(entry));
    
    memcpy((void*)entry, &f_buf[fo->CurrentPos], sizeof(entry));
    
    en->DIR_FstClusHI = entry[20];
    en->DIR_FstClusHI |= (entry[21]<<8);
    en->DIR_FstClusLO = entry[26];
    en->DIR_FstClusLO |= (entry[27]<<8);
    fo->FirstClus = ((en->DIR_FstClusHI<<16) | en->DIR_FstClusLO);
    UMAS_dbg("file fstclus : %d\n", fo->FirstClus);

    filesize = entry[28];
    filesize |= (entry[29]<<8);
    filesize |= (entry[30]<<16);
    filesize |= (entry[31]<<24);

    fo->FileSize = filesize;
    f_size = filesize;
    UMAS_dbg("filesize : %d bytes\n",fo->FileSize);

    fo->CurrentSec = dsk->LBAOfFirstSec+(dsk->reserved)+(dsk->SecPerFAT*dsk->NumberOfFat)+((fo->FirstClus-2)*dsk->SecPerClus);  
    UMAS_dbg("LBA :%d\n", dsk->LBAOfDataArea);
    UMAS_dbg("Current Secr : %d\n",fo->CurrentSec);
    return 0;
}







int get_next_cluster(PDISK_T* pdisk, DISKINFO *dsk)
{
    int i=0;
    int j=0;
    int cnt=0;
    int buf[4096];
    unsigned long secnum=0;
    unsigned long lba=0;
    unsigned long clus_cnt=0;

    memset((void*)buf, 0, sizeof(buf));

    UMAS_dbg("cluster per fat : %d\n", dsk->SecPerFAT / dsk->SecPerClus);
//  if(fo->CurrentClus < dsk->MaxClus;//(dsk->SecPerFAT / dsk->SecPerClus))
    {
        if(dsk->TypeOfFAT == FAT32)
        {
            i= ((fo->CurrentClus)<<2);

            //buf size== 4096, secnum <4096
            while(i>=0x1000)
            {
               i-=0x1000;
               clus_cnt++;
            }

            UMAS_dbg("clus_cnt:%d i:%d\n",clus_cnt, i);
            secnum = dsk->LBAOfFat+(clus_cnt*8);

            umas_disk_read(pdisk, secnum, 8, buf);

            fo->CurrentClus = fat_buf[i];
            fo->CurrentClus |= (fat_buf[i+1]<<8);
            fo->CurrentClus |= (fat_buf[i+2]<<16);
            fo->CurrentClus |= (fat_buf[i+3]<<24);

            fo->CurrentSec = dsk->LBAOfFirstSec+(dsk->reserved)+(dsk->SecPerFAT*dsk->NumberOfFat)+((fo->CurrentClus-2)*dsk->SecPerClus);   

            UMAS_dbg("clus, sec : %d %d\n",fo->CurrentClus, fo->CurrentSec); 

            if(fo->CurrentClus == 0x0FFFFFFF)
            {
                UMAS_dbg("EOC1\n");
                return 1;
            }
            if(fo->CurrentClus == 0)
            {
                UMAS_dbg("current clus 0\n");
                i=0;
                j=0;
                cnt=0;
                secnum=0;
                lba=0;
                clus_cnt=0;
                get_next_cluster(pdisk, dsk);
                dsk->CntOfEntry++;
                if(dsk->CntOfEntry >=5)
                    return 2;
            
            }
        }
        else if(dsk->TypeOfFAT == FAT16)
        {
            umas_disk_read(pdisk, (dsk->LBAOfFat+((fo->CurrentClus)<<1)), 1, fat_buf);

            fo->CurrentClus = fat_buf[0];
            fo->CurrentClus |= (fat_buf[1]<<8);
            UMAS_dbg("clus : %d\n",fo->CurrentClus);

            if(fo->CurrentClus == 0xFFFF)
            {
                UMAS_dbg("EOC2\n");
                return 1;
            }
        }
           
    

    }
    //else
    //{
     //   sysprintf("exceed cluster\n");
     //   return 1;
//  }
    
    return 0;
}

int read_file1(PDISK_T *pdisk, DISKINFO *dsk)
{
    int read_size = 0;
    int ret =0;

    fo->CurrentClus = (fo->FirstClus);

    for(read_size=0; read_size<fo->FileSize; )
    {
       
        UMAS_dbg("curclus %d %d\n",fo->CurrentClus,fo->CurrentSec);
        
        umas_disk_read(pdisk, fo->CurrentSec,dsk->SecPerClus,&file_buf[read_size]);
        memcpy(&file_buf[read_size], &fat_buf[0], (dsk->SecPerClus*dsk->BytePerSec));
        
        read_size+=(dsk->SecPerClus*dsk->BytePerSec);

        ret = get_next_cluster(pdisk, dsk);
        if(ret == 1)
        {
            UMAS_dbg(" EOF\n");
            break;      
        }
        else if(ret == 2)
        {
            UMAS_dbg("fat read fail\n");
            return 4;
        }

        
    }
    return 0;
}


int read_entry(PDISK_T* pdisk,UINT8* data)
{
    int i=0;
    int cluster_pos=0;
    int cluster_hi=0;
    int cluster_lo=0;

    for(i=0; i<512; )
    {
        if(data[i] == 0)
        {
            UMAS_dbg("entry end\n");
            i+=32;
        }
        else if(data[i] == 0xE5)
        {
            UMAS_dbg("eraser file\n");
        }
        else
        {
           
            cluster_hi = (data[i+20]<<8);
            cluster_hi |= data[i+21];
            cluster_lo = (data[i+26]<<8);
            cluster_lo |= data[i+27];
            cluster_pos = (cluster_hi<<16);
            cluster_pos |= cluster_lo;

            if(!strncmp(data[i],filename,8))
            {
                UMAS_dbg("%s\n", data);
                return cluster_pos;
            }
            i+=32;
        }
    }
    UMAS_dbg("no search file\n");
    return 0;
}


int fsPhysicalDiskConnected(PDISK_T *pdisk)
{
    int buf1=0;
    int partition=0;
    int backup=0;
    int ret = 0;


    int volatile j=0;
    find_partition(pdisk); //find partition
  
    for(j = 0; j<4; j++)
    {
        partition = st_partition[j];
        if(partition)
        {
            UMAS_dbg("partition%d read sector 0,1\n",j);

            ret = get_bpb_info(pdisk, &pdiskinfo[j],partition); //display boot sector info
            if(ret == -1)
                ret |= 100000;

            ret = find_file(pdisk, &pdiskinfo[j]);
            if(ret)
            {
                dwstate = ret;
            }

#if 0
            if(ret)
            {
                if(ret != -1)
                {
                    sysprintf("backup partition%d read sector 0,1\n",j);
                    read_file(pdisk,partition+backup,2, buf); //
                   
                    backup = get_bpb_info(pdisk, buf);
                }
                else
                {
                    sysprintf("err boot record, no backup boot\n");
                }
                
            }
#endif
        }

    }



    
    return 0;
}
      

int usb_fw_download(void)
{
    unsigned long adl32=0;
    unsigned long rcv_adl32=0; 
    unsigned long marker = 0x53545259;
    unsigned long rcv_marker = 0;
    unsigned long ret = 0;
    char mark[4]={0};
	dwstate = 0;

    //char backchk_buf[0x200000]={0};
    //unsigned long backchk_len=0;
    

    USB_PortInit(HOST_LIKE_PORT1);

    ret = InitUsbSystem();
    if(ret == -1)
    {
        dwstate |= 0x1000000;
    }

	sysprintf("usb init OK\n");
    ret = UMAS_InitUmasDriver();

    if(ret == -1)
    {
        dwstate |= 0x10000000;
    }
    sysprintf("usb mass storage OK\n");

	Hub_CheckIrqEvent();
    if(dwstate || (ret == -1))
    {
        if(dwstate & 0x100000)
            sysprintf("===========bpb fail\n");
        else if(dwstate & 0x10000)
            sysprintf("===========fatread fail\n");
        else if(dwstate & 0x1000)
            sysprintf("===========marker fail\n");
        else if(dwstate & 0x100)
            sysprintf("===========version fail\n");
        else if(dwstate & 0x10)
            sysprintf("===========adler32 fail\n");
        else if(dwstate & 0x1)
            sysprintf("===========no file\n");
        else if(dwstate & 0x1000000)
            sysprintf("===========init usb error\n");
        else if(dwstate & 0x10000000)
            sysprintf("===========umas storage driver error\n");
    }
    else
    {
        if(usb_detection)
        {
            rcv_marker = mark[0];
            rcv_marker |= (mark[1]<<8);
            rcv_marker |= (mark[2]<<16);
            rcv_marker |= (mark[3]<<24);
            if(marker != rcv_marker)
            {
                sysprintf("Erase 0x200000+0x200000\n");
                spiFlashEraseSector(BACKUP_ADDR,32);//63);

                sysprintf("Write SpiFlash,fsize:%d\n",f_size);
 			    spiFlashWrite(BACKUP_ADDR+0x8000, f_size, file_buf);

            }
            else
            {
                sysprintf("Backup OK\n");
            }


    /*
            spiFlashRead(BACKUP_ADDR+0x8000, 0x200000, backchk_buf);

            backchk_len = backchk_buf[4];
            backchk_len |= (backchk_buf[5]<<8);
            backchk_len |= (backchk_buf[6]<<16);
            backchk_len |= (backchk_buf[7]<<24);

            
            adl32 = adler32(1, &backchk_buf[16], backchk_len);
                
            rcv_adl32 = backchk_buf[12];
            rcv_adl32 |= (backchk_buf[13]<<8);
            rcv_adl32 |= (backchk_buf[14]<<16);
            rcv_adl32 |= (backchk_buf[15]<<24);
            sysprintf("rcv_adl32 : %d, adl32 : %d\n", (unsigned long)rcv_adl32,(unsigned long)adl32);

            if(adl32 != rcv_adl32)
            {
                sysprintf("Erase 0x200000+0x200000\n");
                spiFlashEraseSector(BACKUP_ADDR,32);//63);

                sysprintf("Write SpiFlash,fsize:%d\n",f_size);
                spiFlashWrite(BACKUP_ADDR+0x8000, f_size, file_buf);

            }
            else
            {
                sysprintf("Backup OK\n");
            }
    */
        
            sysprintf("Erase 0x10000+0x1F0000\n");
            spiFlashEraseSector(START_ADDR,31);//63);
            sysprintf("Write SpiFlash,fsize:%d\n",f_size);
 			spiFlashWrite(START_ADDR, f_size, file_buf);

            memset((void*)file_buf, 0, sizeof(file_buf));
        
            sysprintf("Read and Compare SpiFlash\n");
            spiFlashRead(START_ADDR, f_size, file_buf);
        
            adl32 = adler32(1, &file_buf[16], f_size-16);
        
            rcv_adl32 = file_buf[12];
            rcv_adl32 |= (file_buf[13]<<8);
            rcv_adl32 |= (file_buf[14]<<16);
            rcv_adl32 |= (file_buf[15]<<24);

            sysprintf("rcv_adl32 : 0x%X, adl32 : 0x%X\n", rcv_adl32,adl32);

            
            if(adl32 == rcv_adl32)
            {
                adl32 = 0;
                rcv_adl32 = 0;
                sysprintf("compare adler32 OK!!\n");
#if 0
                sysprintf("Erase backup area \n");
                spiFlashEraseSector(BACKUP_ADDR,32);//63);
                sysprintf("Write Backup %dBytes\n",f_size);
			    spiFlashWrite(BACKUP_ADDR+0x8000, f_size, file_buf);
				
                memset((void*)file_buf, 0, sizeof(file_buf));
        
                sysprintf("Back up Read and Compare SpiFlash\n");
                spiFlashRead(BACKUP_ADDR+0x8000, f_size, file_buf);
                adl32 = adler32(1, &file_buf[16], f_size-16);
                        
                rcv_adl32 = file_buf[12];
                rcv_adl32 |= (file_buf[13]<<8);
                rcv_adl32 |= (file_buf[14]<<16);
                rcv_adl32 |= (file_buf[15]<<24);

                sysprintf("rcv_adl32 : 0x%X, adl32 : 0x%X\n", rcv_adl32,adl32);

                if(adl32 == rcv_adl32)
                {
                    sysprintf("Write SpiFlash & Backup OK!!\n");
                }
                else
                {
                    sysprintf("Write SpiFlash OK!! & Backup Fail\n");
                    spiFlashEraseSector(BACKUP_ADDR,32);
                }
#endif
            
            }
            else
            {
                sysprintf("compare adler32 Fail 0x%X,0x%X!!\n",adl32, rcv_adl32);
                spiFlashEraseSector(START_ADDR,31);
            }
        
            sysprintf("Write SpiFlash OK \n");
        }
        else
        {
            sysprintf("no usb detection\n");
        }
    }
}


INT fsPhysicalDiskDisconnected(PDISK_T *pdisk)
{
 return 0;
}



INT fs_last_drive_no(void)
{
    return 0;
}

INT  fsDiskFreeSpace(INT nDriveNo, UINT32 *puBlockSize, UINT32 *puFreeSize, UINT32 *puDiskSize)
{

    return 0;


}

