#pragma once

using namespace Microsoft::WRL;

typedef enum
{
    DATA_UNKNOWN = 0,
    DATA_SAMPLE,
    DATA_MEDIA_TYPE,
    DATA_COMPLETE_ALL_FRAMES,
    DATA_MAX
} eDataType;

class CDataConnector
{
public:
    CDataConnector();
    virtual ~CDataConnector();

    _Check_return_ HRESULT
        Initialize();

    void Shutdown();
    BOOL IsDone()
    {
        return m_fShouldFinish;
    }

    _Check_return_ HRESULT
        ReadData(
            _Out_ eDataType *pDataType,
            _In_ IUnknown **ppSample,
            _In_ DWORD dwMilliseconds
            );

    _Check_return_ HRESULT
        WriteData(
            eDataType dataType,
            _In_ IUnknown *pSample,
            _In_ DWORD dwMilliseconds
            );

private:
    HANDLE m_hSampleReady;
    HANDLE m_hSampleDone;

    volatile BOOL m_fShouldFinish;
    eDataType m_dataType;
    ComPtr<IUnknown> m_pData;
};


