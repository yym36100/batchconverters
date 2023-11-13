// batchconverter.cpp : Defines the entry point for the console application.
//

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <stdio.h>
#include <tchar.h>

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
using namespace Gdiplus;
#pragma comment (lib,"Gdiplus.lib")
#include <strsafe.h>

#include <vector>
#include <iostream>
#include <string>


ULONG_PTR m_gdiplusToken;

std::vector<std::string> myfiles;


typedef unsigned char u8;
typedef unsigned short u16;

class CImg{
public:
	u8*pData;
	u8*pRLE;

	u8 CImg::getByte(int i){
		u8 res = 0;
		for (int j = 0; j < 8; j++) res |= (pData[i * 8 + j] & 1) << j;
		return res;
	}
	void CImg::setByte(int i, u8 b){
		for (int j = 0; j < 8; j++) pData[i * 8 + j] = (b & (1 << j)) ? 0xff : 0;
	}
	u16 CImg::getRLELength(int i){
		u8 cur = getByte(i++);
		u16 len = 1;
		while (cur == getByte(i++))
		{
			if (i > 400 * 240 / 8) {
				break;
			}
		len++;
	}
		return len;
	}

	u16 CImg::encodeRLE(void){
		u8 cur;
		u16 len = 0;
		u16 rlen = 0;
		u8* buff = new u8[400 * 240 / 8];

		for (int i = 0; i < 400 * 240 / 8;){

			cur = getByte(i);
			// get rle length from this location
			rlen = getRLELength(i);
			i += rlen;

			if (rlen > 3){
				//encode rle
				if (rlen <= 255){ buff[len++] = 0x55; buff[len++] = rlen;				buff[len++] = cur; }
				else		    { buff[len++] = 0xaa; buff[len++] = (rlen >> 8) & 0xff; buff[len++] = rlen & 0xff;	buff[len++] = cur; }
			}
			else
			{
				//don't encode rle, only if must
				if (cur == 0x55)     { buff[len++] = 0x55; buff[len++] = rlen;					buff[len++] = cur; }
				else if (cur == 0xaa){ buff[len++] = 0xaa; buff[len++] = 0; buff[len++] = rlen;	buff[len++] = cur; }
				else {
					// nothing to encode
					for (int i = 0; i < rlen; i++)
						buff[len++] = cur;
				}
			}
		}
		pRLE = buff;
		return len;
	}

};

const char *gargv[3];
void convert_file(const char *name){

	// 1. load file

	// build full name
	TCHAR szName[MAX_PATH];
	StringCchCopy(szName, MAX_PATH, gargv[1]);
	StringCchCat(szName, MAX_PATH, TEXT("\\"));
	StringCchCat(szName, MAX_PATH, name);

	wchar_t wfname[255];
	mbstowcs(wfname, szName, strlen(szName) + 1);

	Gdiplus::Bitmap b(wfname);

	u8* pData = new u8[400 * 240];
	Gdiplus::Color c;
	for (int y = 0; y < 240; y++){
		for (int x = 0; x < 400; x++){
			b.GetPixel(x, y, &c);
			pData[(y * 400 + x)] = c.GetB();
		}
	}

	// 2. do conversion, rle
	CImg i;
	i.pData = pData;
	u16 len = i.encodeRLE();
//	printf("rle size = %d\n", len);

	// 3. save file
	char destfile[255];

	strcpy(destfile, gargv[2]);
	strcat(destfile, "\\");
	strcat(destfile, name);
	strcat(destfile, ".1bit");

	FILE *f = fopen(destfile, "wb");
	if (f){
		fwrite(i.pRLE, len, 1, f);
		fclose(f);
	}
}

class myworkers{
public:
	enum {noThreads=16};
	static HANDLE hThread[noThreads];

	static DWORD WINAPI myThreadProc(_In_ LPVOID lpParameter){
		const char *name;
		int i = (int)lpParameter;
		printf("Thread %d started\n", i);
		while (name = pull_job()){
			//printf("%d ",index);
			convert_file(name);
		}
		printf("Thread %d finished\n", i);		
		return 0;
	}

	static void init(){	
		//create the threads
		for (int i = 0; i < noThreads; i++){
			hThread[i] = CreateThread(0, 0, myThreadProc, (LPVOID)i, CREATE_SUSPENDED, 0);
		}
	}

	static void start(){
		for (int i = 0; i < noThreads; i++){
			ResumeThread(hThread[i]);
		}
	}
	static void waitforall(){

		while (1){
			DWORD  res = WaitForMultipleObjects(noThreads, hThread, TRUE,200);
			if (WAIT_TIMEOUT != res) break;
			printf("%d (%f%%)\n", index, (float)(100.0*index / ((float)mpFiles->size())));
		}
	}

	
	static std::vector<std::string> *mpFiles;
	static int index;

	static const char* pull_job(){
		const char *res;
		if (index < mpFiles->size())
		{			
			res= mpFiles->at(index).c_str();
			index++;
			return res;
		}
		else
			return 0;		
	}

	
	static void jobs_init(std::vector<std::string> *pFiles){
		mpFiles = pFiles;
	}
};

HANDLE myworkers::hThread[myworkers::noThreads] = { 0 };
std::vector<std::string> *myworkers::mpFiles;
int myworkers::index = 0;


int _tmain(int argc, _TCHAR* argv[])
{
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);

	if (argc != 3){
		printf("usage: batchconverted <inputpath> <outputpath>\n");
		return 0;
	}
	printf("argc=%d\n", argc);
	printf("input path=%s\n", argv[1]);
	printf("output path=%s\n", argv[2]);

	gargv[1] = argv[1];
	gargv[2] = argv[2];

	WIN32_FIND_DATA ffd;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	TCHAR szDir[MAX_PATH];
	LARGE_INTEGER filesize;
	int i = 0;


	StringCchCopy(szDir, MAX_PATH, argv[1]);
	StringCchCat(szDir, MAX_PATH, TEXT("\\*"));

	hFind = FindFirstFile(szDir, &ffd);

	if (INVALID_HANDLE_VALUE == hFind)
	{
		_tprintf(TEXT("FindFirstFile"));
		return 0;
	}

	do
	{
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			_tprintf(TEXT("  %s   <DIR>\n"), ffd.cFileName);
		}
		else
		{
			filesize.LowPart = ffd.nFileSizeLow;
			filesize.HighPart = ffd.nFileSizeHigh;
			//_tprintf(TEXT("  %s   %ld bytes\n"), ffd.cFileName, filesize.QuadPart);
			myfiles.push_back(ffd.cFileName);
		}
	} while (FindNextFile(hFind, &ffd) != 0);

	printf("no files found = %d\n", myfiles.size());

	myworkers::init();
	myworkers::jobs_init(&myfiles);
	myworkers::start();
	myworkers::waitforall();

#if 0
	for (int i = 0; i < myfiles.size(); i++){
		std::string s = myfiles.at(i);
		const char* p = s.c_str();

		convert_file(p);
		/*TCHAR szName[MAX_PATH];

		StringCchCopy(szName, MAX_PATH, argv[1]);
		StringCchCat(szName, MAX_PATH, TEXT("\\"));
		StringCchCat(szName, MAX_PATH, p);

		printf("%4d: %s ", i, szName);

		{
			wchar_t wfname[255];
			mbstowcs(wfname, szName, strlen(szName) + 1);

			Gdiplus::Bitmap b(wfname);

			u8* pData = new u8[400 * 240];
			Gdiplus::Color c;
			for (int y = 0; y < 240; y++){
				for (int x = 0; x < 400; x++){
					b.GetPixel(x, y, &c);
					pData[(y * 400 + x)] = c.GetB();
				}
			}

			CImg i;
			i.pData = pData;
			u16 len = i.encodeRLE();
			printf("rle size = %d\n", len);
			char destfile[255];

			strcpy(destfile, argv[2]);
			strcat(destfile, "\\");
			strcat(destfile, p);
			{
				FILE *f = fopen(destfile, "wb");
				if (f){
					fwrite(i.pRLE, len, 1, f);
					fclose(f);
				}
			}*/
		
	}
#endif

	Gdiplus::GdiplusShutdown(m_gdiplusToken);
	return 0;
}


