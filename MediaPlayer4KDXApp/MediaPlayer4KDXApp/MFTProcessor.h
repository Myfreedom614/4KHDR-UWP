//*@@@+++@@@@******************************************************************
//
// Microsoft Windows Media Foundation
// Copyright (C) Microsoft Corporation. All rights reserved.
//
//*@@@---@@@@******************************************************************
#pragma once

#include "DataConnector.h"

class CMFTProcessor
{
public:
    CMFTProcessor();
    virtual ~CMFTProcessor();
    virtual HRESULT 
    Initialize(
        _In_ IMFTransform *pMFT,
        _In_ CDataConnector *pMFTInConnector,
        _In_ CDataConnector *pMFTOutConnector
    );
    
    virtual _Check_return_ HRESULT 
    StartMFT() = 0;

    virtual _Check_return_ HRESULT
    Shutdown();
    
protected:
    ComPtr<IMFTransform> m_pMFT;
    CDataConnector *m_pMFTInConnector;
    CDataConnector *m_pMFTOutConnector;
    HANDLE m_hThread;
};

class CAsyncMFTProcessor:public CMFTProcessor/*,
    public LiveObjectTracker<CAsyncMFTProcessor>*/
{
public:
    ~CAsyncMFTProcessor();
    _Check_return_  HRESULT StartMFT();
    
private:
    static DWORD WINAPI 
    AsyncMFThread(
        _In_ LPVOID lpParam
    );

    _Check_return_ HRESULT RunAsyncThread();
};

class CSyncMFTProcessor:public CMFTProcessor/*,
    public LiveObjectTracker<CSyncMFTProcessor>*/
{
public:
    ~CSyncMFTProcessor();
    _Check_return_ HRESULT
    StartMFT();

private:
    static DWORD WINAPI
    SyncMFThread(
        _In_ LPVOID lpParam
    );

    _Check_return_ HRESULT RunSyncThread();

};


