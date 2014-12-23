#include <windows.h>
#include <Shlwapi.h>
#include <iostream>
#include <TextCompress.h>

#pragma comment(lib,"TextCompressLib.lib")
#pragma comment(lib,"Shlwapi.lib")

int main(int argc, char *argv[])
{
	{
		SetConsoleTitle(L"Minimal Non-Redundant Coding for Lossless Compression");

		if (argc < 4 || argc > 5 || strcmp(argv[1],"-h") == 0 || strcmp(argv[1],"-H") == 0)
		{
			std::cout <<"MNRC-LC" << std::endl<< "Minimal Non-Redundant Coding for Lossless Compression" << std::endl
				<< "by Antonio Esposito and Vincenzo Chianese" << std::endl << 
				"Esegue la compressione/decompressione lossless del file indicato mediante la codifica MNRC-LC" << std::endl <<
				"exeFile -C | -D [drive:][path]filename [drive:][path]outfilename [Power]" <<std::endl
				<< "	-C: \t\t comprime il file in input" << std::endl
				<< "	-D: \t\t decomprime il file in input" << std::endl
				<< "	Power: \t\t specifica la potenza di compressione. Se omesso, verra' impostato a 1 di default" << std::endl;
			return 0;
		}

		std::cout <<"Opening " << argv[2] << std::endl;
		HANDLE h = CreateFileA(argv[2],GENERIC_READ,0,0,OPEN_EXISTING,0,0);
		if (h == INVALID_HANDLE_VALUE)
		{
			std::cout <<"Error in opening";
			return -1;
		}

		DWORD fs = GetFileSize(h,0);
		if (fs == INVALID_FILE_SIZE || fs == 0)
		{
			std::cout << "Error in file size";
			CloseHandle(h);
			return -1;
		}

		DWORD r;

		char *data = new char[fs];
		void *cdata;
		memset(data,0,fs);
		BOOL b = ReadFile(h,data,fs,&r,0);
		CloseHandle(h);

		ITextCompressor *t;

		if (fs % 3 == 0)
			t = CreateTextCompressor(3);
		else if (fs % 2 == 0)
			t = CreateTextCompressor(2);
		else
			t = CreateTextCompressor(1);

		int newlen = 0;

		char *outpath = NULL;

		if (strcmp(argv[1],"-c") == 0 || strcmp(argv[1],"-C") == 0)
		{

			newlen = t->Compress(data,fs,(argc>= 4 ? atoi(argv[4]) : 1),&cdata);
			outpath = (char*)malloc(strlen(argv[3]) + 5);
			strcpy(outpath,argv[3]);
			strcat(outpath,".mnr");
		}
		else if (strcmp(argv[1],"-d") == 0 || strcmp(argv[1],"-D") == 0)
		{
			newlen = t->Decompress(data,fs,&cdata);
			outpath = strdup(argv[3]);
		}

		h = CreateFileA(outpath,GENERIC_ALL,0,0,CREATE_ALWAYS,0,0);

		if (h == INVALID_HANDLE_VALUE)
		{
			std::cout << "Error in file size";
			return -1;
		}

		WriteFile(h,cdata,newlen,&fs,0);
		CloseHandle(h);

		free(outpath);
		delete[] data;
		delete[] cdata;
		delete t;
	}
	_CrtDumpMemoryLeaks();
	return 0;
}
