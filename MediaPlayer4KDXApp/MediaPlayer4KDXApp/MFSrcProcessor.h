
#pragma once
#include "DataConnector.h"

#define MAX_STREAM 16
#define MAX_FILE_LEN 2048

class CMFSrcProcessor;

typedef struct
{
    CMFSrcProcessor *pThis;
    IMFMediaStream *pStream;
    CDataConnector *pDataConnecor;
} StreamData;

class CMFSrcProcessor
{
public:
    CMFSrcProcessor();
    ~CMFSrcProcessor();

    HRESULT Initilize(int nSrcNameLen, wchar_t *pSrcName, CDataConnector *pControlConnector,  CDataConnector *pAudioConnector, CDataConnector *pVideoConnector);
    HRESULT Start();
    HRESULT Stop();
    HRESULT WaitToFinish(DWORD timeOut);

private:
    volatile bool m_bRunning;
    volatile bool m_bIsEOF;

    wchar_t m_mediaFile[MAX_FILE_LEN];

    CDataConnector *m_pAudioConnector;
    CDataConnector *m_pVideoConnector;
    CDataConnector *m_pControlConnector;

    volatile bool m_bSourceStopped;

    HANDLE m_hSourceThread;
    HANDLE m_hStreamThread[MAX_STREAM];
    StreamData m_pStreams[MAX_STREAM];

    static DWORD WINAPI StaticSourceThread(_In_ LPVOID lpParam);
    HRESULT SourceThread();

    static DWORD WINAPI StaticStreamThread(_In_ LPVOID lpParam);
    HRESULT StreamThread(IMFMediaStream *pStream, CDataConnector *pDataConnector);
};


