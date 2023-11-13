#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <stdio.h>
#include <tchar.h>
#include <windows.h>

#include <vector>
#include <iostream>
#include <string>

#include <strsafe.h>

struct stmyfiles{
	std::string name;
	unsigned long length;
};

std::vector<stmyfiles> gFilesVect;
//std::vector<stmyfiles> myfiles;

typedef unsigned char u8;
typedef unsigned short u16;

const char *gargv[3];

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
		while (cur == getByte(i++))len++;
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
	u16 CImg::decodeRLE(u8 *pR){
		u8 cur;

		u16 bcnt = 0;
		u8 *pRo = pR;

		pData = new u8[400 * 240];	

		while (1){
			cur = *pR++;			
			if (cur == 0x55){
				u8 len = *pR++;
				u8 val = *pR++;			
				for (int i = 0; i<len; i++){
					setByte(bcnt++, val);
				}
			}
			else if (cur == 0xaa){
				u8 hl = *pR++;
				u8 ll = *pR++;
				u16 len = ((hl) << 8) + (ll);
				u8 val = *pR++;				
				for (int i = 0; i<len; i++){
					setByte(bcnt++, val);
				}
			}
			else {				
				setByte(bcnt++, cur);
			}
			if (bcnt >= 400 * 240 / 8)
				break;
		}
		return bcnt;		
	}

};

std::vector<u8*> myImages;
void decpmp_file(const char *name){

	// build full name
	char szName[MAX_PATH];
	strcpy(szName, gargv[1]); strcat(szName, "\\"); strcat(szName, name);
	
	FILE *f = fopen(szName, "rb");
	if (!f) return;

	u8 rlebuff[2 * 400 * 240 / 8];
	size_t res;
	res = fread(rlebuff, 1,2 * 400 * 240 / 8, f);
	fclose(f);

	//u8 *pData = new u8[400 * 240];
	CImg i;
	u16 recsize;
	//i.pData = pData;
	recsize = i.decodeRLE(rlebuff);
	
	myImages.push_back(i.pData);
	//printf("%s filesize = %d recsize=%d\n", name,res,recsize);

}

class myworkers{
public:
	enum { noThreads = 1 };
	static HANDLE hThread[noThreads];
	static std::vector<stmyfiles> *mpFiles;
	static int index;

	static DWORD WINAPI myThreadProc(_In_ LPVOID lpParameter){
		const char *name;
		int i = (int)lpParameter;
		printf("Thread %d started\n", i);
		while (name = pull_job()){
			decpmp_file(name);
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
			DWORD  res = WaitForMultipleObjects(noThreads, hThread, TRUE, 200);
			if (WAIT_TIMEOUT != res) break;
			printf("%d (%f%%)\n", index, (float)(100.0*index / ((float)mpFiles->size())));
		}
		printf("%d (%f%%)\n", index, (float)(100.0*index / ((float)mpFiles->size())));
	}

	static const char* pull_job(){
		const char *res;
		if (index < mpFiles->size())
		{
			res = mpFiles->at(index).name.c_str();
			index++;
			return res;
		}
		else
			return 0;
	}

	static void jobs_init(std::vector<stmyfiles> *pFiles){
		mpFiles = pFiles;
	}
};

void diff_file(u16 idx,const u8* b1, const u8* b2){
	u8 diff[400 * 240];
	for (int i = 0; i < 400 * 200; i++){
		diff[i] = b1[i] ^ b2[i];
	}
	CImg im;
	int sz;
	im.pData = diff;
	sz = im.encodeRLE();
	int delta = -sz + gFilesVect.at(idx).length;
	if (delta > 0){
		printf("%d %x,%x sz=%d orig=%d delta=%d **************\n", idx, b1, b2, sz, gFilesVect.at(idx).length, delta);

		// 3. save file
		char destfile[255];

		strcpy(destfile, gargv[2]);
		strcat(destfile, "\\");
		strcat(destfile, gFilesVect.at(idx).name.c_str());
		//strcat(destfile, ".1bit");

		FILE *f = fopen(destfile, "wb");
		if (f){
			fwrite(im.pRLE, sz, 1, f);
			fclose(f);
		}

	}
	else printf("%d %x,%x sz=%d orig=%d \n", idx, b1, b2, sz, gFilesVect.at(idx - 1).length);
}

class myworkers2 :public myworkers{
public:
	static std::vector<u8*>*pImages;

	static void init(){
		//create the threads
		for (int i = 0; i < noThreads; i++){
			hThread[i] = CreateThread(0, 0, myThreadProc, (LPVOID)i, CREATE_SUSPENDED, 0);
		}
	}
	static u8 pull_job(u16*idx, u8** b1, u8** b2){
		u8 res = 1;
		if (index < pImages->size() - 1){
			*b1 = (pImages->at(index++));
			*b2 = (pImages->at(index));

			*idx = index;
		}
		else res = 0;
		return res;
	}
	static DWORD WINAPI myThreadProc(_In_ LPVOID lpParameter){
		const char *name;
		int ii = (int)lpParameter;
		printf("Thread %d started\n", ii);
		 u8  *b1=0, *b2=0;
		 u16 i;
		while (pull_job(&i,&b1,&b2)){
			diff_file(i,b1, b2);
		}
		printf("Thread %d finished\n", ii);
		return 0;
	}
	static void jobs_init(std::vector<u8*>*pI) {
		index = 0;
		pImages = pI;
	}
};
std::vector<u8*>* myworkers2::pImages;
//static std::vector<u8*>*pImages;



HANDLE myworkers::hThread[myworkers::noThreads] = { 0 };
std::vector<stmyfiles> *myworkers::mpFiles;
int myworkers::index = 0;

int create_file_list(char *path, std::vector<stmyfiles> *pFilesVect){
	WIN32_FIND_DATA ffd;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	TCHAR szDir[MAX_PATH];
	//LARGE_INTEGER filesize;
	int i = 0;

	StringCchCopy(szDir, MAX_PATH, path); StringCchCat(szDir, MAX_PATH, TEXT("\\*"));

	hFind = FindFirstFile(szDir, &ffd);

	if (INVALID_HANDLE_VALUE == hFind) return 0;
	
	int cnt = 0;
	do
	{
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			_tprintf(TEXT("  %s   <DIR>\n"), ffd.cFileName);
		}
		else{
			//filesize.LowPart = ffd.nFileSizeLow;
			//filesize.HighPart = ffd.nFileSizeHigh;		
			stmyfiles s;
			s.name = ffd.cFileName;
			s.length = ffd.nFileSizeLow;
			pFilesVect->push_back(s);
			//if(cnt++==50)break;
		}
	} while (FindNextFile(hFind, &ffd) != 0);
	
	return pFilesVect->size();
}

int main(int argc, char* argv[])
{
	int res;
	if (argc != 3) return -1;
	gargv[1] = argv[1];
	gargv[2] = argv[2];

	res = create_file_list(argv[1], &gFilesVect);
	printf("%d files found\n", res);

	myworkers::init();
	myworkers::jobs_init(&gFilesVect);
	myworkers::start();
	myworkers::waitforall();

	myworkers2::init();
	myworkers2::jobs_init(&myImages);
	myworkers2::start();
	myworkers2::waitforall();
	return 0;
}

