//*@@@+++@@@@******************************************************************
//
// Microsoft Windows Media Foundation
// Copyright (C) Microsoft Corporation. All rights reserved.
//
//*@@@---@@@@******************************************************************

#include "pch.h"
//#include "MfMediaSource.h"
#include "mfidl.h"
#include "Mfapi.h"
#include "MFError.h"
#include "MFTProcessor.h"

#define IFC(_hr_) { hr = (_hr_); if (FAILED(hr)) goto Cleanup;}
#define SAFE_RELEASE(obj) { if (obj) {(obj)->Release(); (obj) = nullptr; };}

#define MAX_WAIT_FOR_DRAIN_COMPLETE 50


CMFTProcessor::CMFTProcessor()
{
    m_pMFTInConnector = nullptr;
    m_pMFTOutConnector = nullptr;
    m_hThread = NULL;
}

CMFTProcessor::~CMFTProcessor()
{
}


_Check_return_ HRESULT
CMFTProcessor::Initialize(
    _In_ IMFTransform *pMFT,
    _In_ CDataConnector *pMFTInConnector,
    _In_ CDataConnector *pMFTOutConnector

)
{
    HRESULT hr = S_OK;

    //IFCVALID(pMFT);
    //IFCVALID(pMFTInConnector);
    //IFCVALID(pMFTOutConnector);

    m_pMFT = pMFT;
    m_pMFTInConnector = pMFTInConnector;
    m_pMFTOutConnector = pMFTOutConnector;
    m_hThread = NULL;

//Cleanup:
    return hr;
}


_Check_return_ HRESULT
CMFTProcessor::Shutdown()
{
    HRESULT hr = S_OK;

    if (m_hThread)
    {
        WaitForSingleObject(m_hThread, INFINITE);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }

    return hr;
}

CAsyncMFTProcessor::~CAsyncMFTProcessor()
{
}


DWORD WINAPI
CAsyncMFTProcessor::AsyncMFThread(_In_ LPVOID lpParam)
{
    HRESULT hr = S_OK;
    CAsyncMFTProcessor *pMFTProc = (CAsyncMFTProcessor *)lpParam;

    hr = pMFTProc->RunAsyncThread();

    return hr;
}

_Check_return_ HRESULT
CAsyncMFTProcessor::RunAsyncThread()
{
    HRESULT hr = S_OK;
    HRESULT hrShutdown = S_OK;
    //BOOL fRunLoop = TRUE;
    MFT_OUTPUT_DATA_BUFFER mftOutputDataBuffer = { 0 };
    BOOL bInputSampleSent = FALSE;
    DWORD dwNoEventCount = 0;

    ComPtr<IMFMediaEventGenerator> pVideoMFTEvents;
    ComPtr<IMFShutdown> pMFTShutdown;

    IFC(m_pMFT.As(&pVideoMFTEvents));

    while(!m_pMFTInConnector->IsDone())
    {
        MediaEventType meType = MEUnknown;

        ComPtr<IMFMediaEvent> pVideoMFTEvent;

        hr = pVideoMFTEvents->GetEvent(MF_EVENT_FLAG_NO_WAIT, &pVideoMFTEvent);

        if (hr == MF_E_NO_EVENTS_AVAILABLE)
        {
            //Nothing to do
            Sleep(1);
            hr = S_OK;
            continue;
        }
        IFC(hr);

        IFC(pVideoMFTEvent->GetType(&meType));
        if (meType == METransformNeedInput)
        {
            ComPtr<IUnknown> spData;
            ComPtr<IMFSample> spInSample;
            eDataType dataType;
            
            hr = m_pMFTInConnector->ReadData(&dataType, &spData, INFINITE);
            if (m_pMFTInConnector->IsDone() || (SUCCEEDED(hr) && (dataType == DATA_COMPLETE_ALL_FRAMES)))
            {
                break;
            }
            IFC(hr);

            (spData.As(&spInSample));
            IFC(m_pMFT->ProcessInput (0, spInSample.Get(), 0));
            bInputSampleSent = TRUE;
        }
        else if (meType == METransformHaveOutput)
        {

            //Got encoded video
            MFT_OUTPUT_STREAM_INFO outputStreamInfo;
            ComPtr<IMFSample> pOutputSample;

            DWORD status = 0;
            memset(&mftOutputDataBuffer, 0, sizeof(mftOutputDataBuffer));

            IFC(m_pMFT->GetOutputStreamInfo(0, &outputStreamInfo));
            if ((outputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0)
            {
                ComPtr<IMFMediaBuffer> pOutputBuffer;
                IFC(MFCreateAlignedMemoryBuffer(outputStreamInfo.cbSize, outputStreamInfo.cbAlignment, &pOutputBuffer));
                IFC(MFCreateSample(&pOutputSample));
                IFC(pOutputSample->AddBuffer(pOutputBuffer.Get()));
                mftOutputDataBuffer.dwStreamID = 0;
                mftOutputDataBuffer.pSample = pOutputSample.Detach();
            }
            else
            {
                mftOutputDataBuffer.pSample = NULL;
            }

            HRESULT processOutputResult = m_pMFT->ProcessOutput( 0, 1, &mftOutputDataBuffer, &status );

            if (processOutputResult == MF_E_TRANSFORM_STREAM_CHANGE)
            {
                ComPtr<IMFMediaType> pMediaType;
                ComPtr<IUnknown> spData;

                IFC(m_pMFT->GetOutputAvailableType(0, 0, &pMediaType));
                IFC(m_pMFT->SetOutputType(0, pMediaType.Get(), 0));

                IFC(pMediaType.As(&spData));
                hr = m_pMFTOutConnector->WriteData(DATA_MEDIA_TYPE, spData.Get(), INFINITE);
                if (m_pMFTInConnector->IsDone())
                {
                    break;
                }
                IFC(hr);
                
            }
            else if (SUCCEEDED(processOutputResult))
            {
                ComPtr<IUnknown> spData;

                mftOutputDataBuffer.pSample->QueryInterface(IID_PPV_ARGS(&spData));
                hr = m_pMFTOutConnector->WriteData(DATA_SAMPLE, spData.Get(), INFINITE);
                if (m_pMFTInConnector->IsDone())
                {
                    break;
                }
                IFC(hr);
            }
            else
            {
                IFC(processOutputResult);
            }
            SAFE_RELEASE(mftOutputDataBuffer.pSample);
            SAFE_RELEASE(mftOutputDataBuffer.pEvents);
        }
    }

    //Release if anything pending
    SAFE_RELEASE(mftOutputDataBuffer.pSample);
    SAFE_RELEASE(mftOutputDataBuffer.pEvents);

    // Notify the MFT of End of stream to allow it to cleanup.
    IFC(m_pMFT->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0));
    IFC(m_pMFT->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0));

    // Intel MFT does not send drained event if not input is sent. 
    // Also an optimization/workaround is not to wait for drained event if not sample is sent as input

    while (bInputSampleSent)
    {
        MediaEventType meType = MEUnknown;

        ComPtr<IMFMediaEvent> pVideoMFTEvent;
        hr = pVideoMFTEvents->GetEvent(MF_EVENT_FLAG_NO_WAIT, &pVideoMFTEvent);

        if (hr == MF_E_NO_EVENTS_AVAILABLE)
        {
            dwNoEventCount ++;
            if (dwNoEventCount > MAX_WAIT_FOR_DRAIN_COMPLETE)
            {
                // Waited long for drain complete; just bail out and shutdown
                break;
            }
            //Nothing to do
            Sleep(1);
            hr = S_OK;
            continue;
        }
        IFC(hr);

        IFC(pVideoMFTEvent->GetType(&meType));
        if (meType == METransformHaveOutput)
        {
            //Drain all processed frames. It is very similar to previous ProcessOutput part. 
            //Difference here is just get the samples and release them.

            MFT_OUTPUT_STREAM_INFO outputStreamInfo;
            ComPtr<IMFSample> pOutputSample;

            DWORD status = 0;
            memset(&mftOutputDataBuffer, 0, sizeof(mftOutputDataBuffer));

            IFC(m_pMFT->GetOutputStreamInfo(0, &outputStreamInfo));
            if ((outputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0)
            {
                ComPtr<IMFMediaBuffer> pOutputBuffer;
                IFC(MFCreateAlignedMemoryBuffer(outputStreamInfo.cbSize, outputStreamInfo.cbAlignment, &pOutputBuffer));
                IFC(MFCreateSample(&pOutputSample));
                IFC(pOutputSample->AddBuffer(pOutputBuffer.Get()));
                mftOutputDataBuffer.dwStreamID = 0;
                mftOutputDataBuffer.pSample = pOutputSample.Detach();
            }
            else
            {
                mftOutputDataBuffer.pSample = NULL;
            }

            HRESULT processOutputResult = m_pMFT->ProcessOutput(0, 1, &mftOutputDataBuffer, &status);
            if (SUCCEEDED(processOutputResult))
            {
                ComPtr<IUnknown> spData;

                mftOutputDataBuffer.pSample->QueryInterface(IID_PPV_ARGS(&spData));
                hr = m_pMFTOutConnector->WriteData(DATA_SAMPLE, spData.Get(), 100);
            }

            SAFE_RELEASE(mftOutputDataBuffer.pSample);
            SAFE_RELEASE(mftOutputDataBuffer.pEvents);
        }
        else if (meType == METransformDrainComplete)
        {
            break;
        }
    }

    //Let the consumer know no more data coming
    hr = m_pMFTOutConnector->WriteData(DATA_COMPLETE_ALL_FRAMES, nullptr, INFINITE);

    //Release if anything pending
    SAFE_RELEASE(mftOutputDataBuffer.pSample);
    SAFE_RELEASE(mftOutputDataBuffer.pEvents);

Cleanup:
    m_pMFT->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    m_pMFT->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);

    // Shutdown the MFT always, even on errors.
    hrShutdown = m_pMFT.As(&pMFTShutdown);

    if (SUCCEEDED(hrShutdown))
    {
        hrShutdown = pMFTShutdown->Shutdown();
    }

    //IFC_DIAGONLY(hrShutdown);
    
    SAFE_RELEASE(mftOutputDataBuffer.pSample);
    SAFE_RELEASE(mftOutputDataBuffer.pEvents);

    // If MF is shutting down return S_OK.
    if (hr == MF_E_SHUTDOWN)
    {
        hr = S_OK;
    }

    return hr;
}


_Check_return_ HRESULT
CAsyncMFTProcessor::StartMFT()
{
    HRESULT hr = S_OK;

    m_hThread = CreateThread(NULL, 0, CAsyncMFTProcessor::AsyncMFThread, this, 0, NULL);
    DX::TIF((m_hThread == NULL) ? HRESULT_FROM_WIN32(GetLastError()) : S_OK);
//Cleanup:
    return hr;
}

CSyncMFTProcessor::~CSyncMFTProcessor()
{
}

DWORD WINAPI
CSyncMFTProcessor::SyncMFThread(_In_ LPVOID lpParam)
{
    HRESULT hr = S_OK;
    CSyncMFTProcessor *pMFTProc = (CSyncMFTProcessor *)lpParam;

    hr = pMFTProc->RunSyncThread();
    return hr;
}

_Check_return_ HRESULT
CSyncMFTProcessor::RunSyncThread()
{
    HRESULT hr=S_OK;
    MFT_OUTPUT_DATA_BUFFER mftOutputDataBuffer = { 0 };
    DWORD nInputSample = 0;
    ComPtr<IMFSample> spInSample;

    while(!m_pMFTInConnector->IsDone())
    {

        MFT_OUTPUT_STREAM_INFO outputStreamInfo = {};

        ComPtr<IMFSample> pOutputSample;
        DWORD status = 0;
        memset(&mftOutputDataBuffer, 0, sizeof(mftOutputDataBuffer));

        IFC(m_pMFT->GetOutputStreamInfo(0, &outputStreamInfo));
        if ((outputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0)
        {
            ComPtr<IMFMediaBuffer> pOutputBuffer;
            IFC(MFCreateAlignedMemoryBuffer(outputStreamInfo.cbSize, outputStreamInfo.cbAlignment, &pOutputBuffer));
            IFC(MFCreateSample(&pOutputSample));
            IFC(pOutputSample->AddBuffer(pOutputBuffer.Get()));
            mftOutputDataBuffer.dwStreamID = 0;
            mftOutputDataBuffer.pSample = pOutputSample.Detach();
        }
        else
        {
            mftOutputDataBuffer.pSample = NULL;
        }

        HRESULT syncHr = m_pMFT->ProcessOutput( 0, 1, &mftOutputDataBuffer, &status );
        if (syncHr == MF_E_TRANSFORM_STREAM_CHANGE)
        {
            ComPtr<IMFMediaType> pMediaType;
            ComPtr<IUnknown> spData;

            IFC(m_pMFT->GetOutputAvailableType(0, 0, &pMediaType));
            IFC(m_pMFT->SetOutputType(0, pMediaType.Get(), 0));

            IFC(pMediaType.As(&spData));
            hr = m_pMFTOutConnector->WriteData(DATA_MEDIA_TYPE, spData.Get(), INFINITE);
            if (m_pMFTInConnector->IsDone())
            {
                break;
            }
            IFC(hr);

            syncHr = S_OK;;
        }
        else if (syncHr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        {
            ComPtr<IUnknown> spData;
            eDataType dataType;

            if (spInSample == nullptr)
            {
                hr = m_pMFTInConnector->ReadData(&dataType, &spData, INFINITE);

                if (m_pMFTInConnector->IsDone() || (SUCCEEDED(hr) && (dataType == DATA_COMPLETE_ALL_FRAMES)))
                {
                    break;
                }
                IFC(hr);
                IFC(spData.As(&spInSample));
            }

            IFC(m_pMFT->ProcessInput (0, spInSample.Get(), 0));
            spInSample = nullptr;
            nInputSample ++;
        }
        else if (SUCCEEDED(syncHr) )
        {
            ComPtr<IUnknown> spData;

            mftOutputDataBuffer.pSample->QueryInterface(IID_PPV_ARGS(&spData));
            hr = m_pMFTOutConnector->WriteData(DATA_SAMPLE, spData.Get(), INFINITE);

            if (m_pMFTInConnector->IsDone())
            {
                break;
            }
            IFC(hr);
        }
        else
        {
            IFC(syncHr);

            // Control should not come here. But if it does, it would generate a tight wait loop
            // Hence adding a sleep to avoid tight loop
            Sleep(1);
        }

        SAFE_RELEASE(mftOutputDataBuffer.pSample);
        SAFE_RELEASE(mftOutputDataBuffer.pEvents);

        // See if input data is ready
        if (spInSample == nullptr)
        {
            ComPtr<IUnknown> spData;
            eDataType dataType;

            hr = m_pMFTInConnector->ReadData(&dataType, &spData, 0);

            if (m_pMFTInConnector->IsDone() || (SUCCEEDED(hr) && (dataType == DATA_COMPLETE_ALL_FRAMES)))
            {
                break;
            }


            if (hr == E_FAIL)
            {
                // data is not ready, that's fine
                hr = S_OK;
            }
            else
            {
                IFC(hr);
                IFC(spData.As(&spInSample));
            }
        }

        //If input is ready, see if decode can accept it
        if (spInSample)
        {
            hr = m_pMFT->ProcessInput(0, spInSample.Get(), 0);
            if (SUCCEEDED(hr))
            {
                spInSample = nullptr;
                nInputSample++;
            }
            else if (hr == MF_E_NOTACCEPTING)
            {
                //MFT cannot take data, that fine too
                hr = S_OK;
            }
            else
            {
                IFC(hr);
            }
        }
        

        //fRunLoop = !m_pMFTInConnector->IsDone();
    }

    //Release if anything pending
    SAFE_RELEASE(mftOutputDataBuffer.pSample);
    SAFE_RELEASE(mftOutputDataBuffer.pEvents);

    // Notify the MFT of End of stream to allow it to cleanup.
    IFC(m_pMFT->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0));
    IFC(m_pMFT->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0));

    // Optimization: if not sample is sent, no need to wait for drained message
    while (nInputSample > 0)
    {

        MFT_OUTPUT_STREAM_INFO outputStreamInfo;

        ComPtr<IMFSample> pOutputSample;
        DWORD status = 0;
        memset(&mftOutputDataBuffer, 0, sizeof(mftOutputDataBuffer));

        IFC(m_pMFT->GetOutputStreamInfo(0, &outputStreamInfo));
        if ((outputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0)
        {
            ComPtr<IMFMediaBuffer> pOutputBuffer;
            IFC(MFCreateAlignedMemoryBuffer(outputStreamInfo.cbSize, outputStreamInfo.cbAlignment, &pOutputBuffer));
            IFC(MFCreateSample(&pOutputSample));
            IFC(pOutputSample->AddBuffer(pOutputBuffer.Get()));
            mftOutputDataBuffer.dwStreamID = 0;
            mftOutputDataBuffer.pSample = pOutputSample.Detach();
        }
        else
        {
            mftOutputDataBuffer.pSample = NULL;
        }

        HRESULT syncHr = m_pMFT->ProcessOutput( 0, 1, &mftOutputDataBuffer, &status );
        if (syncHr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        {
            break;
        }
        else if (SUCCEEDED(syncHr))
        {
            ComPtr<IUnknown> spData;

            mftOutputDataBuffer.pSample->QueryInterface(IID_PPV_ARGS(&spData));
            hr = m_pMFTOutConnector->WriteData(DATA_SAMPLE, spData.Get(), 100);
        }
        SAFE_RELEASE(mftOutputDataBuffer.pSample);
        SAFE_RELEASE(mftOutputDataBuffer.pEvents);
    }

    hr = m_pMFTOutConnector->WriteData(DATA_COMPLETE_ALL_FRAMES, nullptr, INFINITE);

Cleanup:
    SAFE_RELEASE(mftOutputDataBuffer.pSample);
    SAFE_RELEASE(mftOutputDataBuffer.pEvents);

    m_pMFT->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    m_pMFT->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);


    if (hr == MF_E_SHUTDOWN)
    {
        hr = S_OK;
    }
    return hr;
}

_Check_return_ HRESULT
CSyncMFTProcessor::StartMFT()
{
    HRESULT hr = S_OK;

    m_hThread = CreateThread(NULL, 0, CSyncMFTProcessor::SyncMFThread, this, 0, NULL);
    DX::TIF((m_hThread == NULL) ? HRESULT_FROM_WIN32(GetLastError()) : S_OK);
//Cleanup:
    return hr;
}

