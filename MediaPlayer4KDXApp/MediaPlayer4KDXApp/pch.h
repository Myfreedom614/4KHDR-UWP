#pragma once

#include <wrl.h>
#include <wrl/client.h>
#include <d3d11_4.h>
#include <d2d1_2.h>
#include <d2d1effects_1.h>
#include <dwrite_2.h>
#include <wincodec.h>
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <memory>
#include <agile.h>
#include <concrt.h>

namespace DX
{
	inline void TIF(HRESULT hr)
	{
		if (FAILED(hr))
		{
			// Set a breakpoint on this line to catch DirectX API errors
			throw Platform::Exception::CreateException(hr);
		}
	}
}
