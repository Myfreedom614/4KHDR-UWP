#include "pch.h"
//#include "MfMediaSource.h"
#include "mfidl.h"
#include "MFError.h"
#include "DataConnector.h"

CDataConnector::CDataConnector()
{
    m_hSampleReady = NULL;
    m_hSampleDone = NULL;
    m_fShouldFinish = FALSE;
    m_dataType = DATA_UNKNOWN;
}

CDataConnector::~CDataConnector()
{
    if (m_hSampleReady != NULL)
    {
        CloseHandle(m_hSampleReady);
        m_hSampleReady = NULL;
    }

    if (m_hSampleDone != NULL)
    {
        CloseHandle(m_hSampleDone);
        m_hSampleDone = NULL;
    }
}

_Check_return_ HRESULT
CDataConnector::Initialize()
{
    HRESULT hr = S_OK;
    m_hSampleReady = CreateSemaphore(NULL, 0, 1, NULL);
    DX::TIF((m_hSampleReady == NULL) ? HRESULT_FROM_WIN32(GetLastError()) : S_OK);

    // At the begining, there would be a check
    // if it's ok to set output sample before any write sample.
    // Hence m_hSampleDone need to srart from 1
    m_hSampleDone = CreateSemaphore(NULL, 1, 1, NULL);
    DX::TIF((m_hSampleDone == NULL) ? HRESULT_FROM_WIN32(GetLastError()) : S_OK);
    return hr;
}

void
CDataConnector::Shutdown()
{

    m_fShouldFinish = TRUE;

    // Release any read or write that might be blocking
    ReleaseSemaphore(m_hSampleReady, 1, NULL);
    ReleaseSemaphore(m_hSampleDone, 1, NULL);
}



_Check_return_  HRESULT
CDataConnector::WriteData(eDataType dataType, IUnknown *pData, DWORD dwMilliseconds)
{
    HRESULT hr = S_OK;
    DWORD waitObj;

    if (!m_fShouldFinish)
    {
        //IFCVALID(pSample);
        waitObj = WaitForSingleObject(m_hSampleDone, dwMilliseconds);
        if (m_fShouldFinish)
        {
            hr = MF_E_SHUTDOWN;
        }
        else if (waitObj == WAIT_OBJECT_0)
        {
            m_pData = pData;
            m_dataType = dataType;
            ReleaseSemaphore(m_hSampleReady, 1, NULL);
        }
        else
        {
            hr = E_FAIL;
        }
    }
    else
    {
        hr = MF_E_SHUTDOWN;
    }

    return hr;

}

_Check_return_ HRESULT
CDataConnector::ReadData(eDataType *pDataType, IUnknown **ppData, DWORD dwMilliseconds)
{
    HRESULT hr = S_OK;
    DWORD waitObj;

    *ppData = nullptr;
    if (!m_fShouldFinish)
    {
        waitObj = WaitForSingleObject(m_hSampleReady, dwMilliseconds);
        if (m_fShouldFinish)
        {
            hr = MF_E_SHUTDOWN;
        }
        else if (waitObj == WAIT_OBJECT_0)
        {
            //IFCPTR(m_pSample);
            *ppData = m_pData.Detach();
            *pDataType = m_dataType;
            m_dataType = DATA_UNKNOWN;
            ReleaseSemaphore(m_hSampleDone, 1, NULL);
        }
        else
        {
            hr = E_FAIL;
        }
    }
    else
    {
        hr = MF_E_SHUTDOWN;
    }

    //Cleanup:
    return hr;

}


