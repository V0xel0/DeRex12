#pragma once
#define AssertHR(hr) AlwaysAssert(SUCCEEDED(hr))

//? For now design is basically as one big sequential state machine (which is bad), since deffered context sucks
// anyway, this is what is being done for fast creation-iteration/research purpose of the application
struct DX_Machine
{
	ID3D11Device5* device;
	IDXGISwapChain4* swap_chain;
	ID3D11RenderTargetView* target_view;
	ID3D11DepthStencilView* depth_stencil_view;

	b32 is_initalized;

	~DX_Machine()
	{
		if (swap_chain)
			swap_chain->Release();
		if (device)
			device->Release();
		if (depth_stencil_view)
			depth_stencil_view->Release();
		if (target_view)
			target_view->Release();
	}
};

struct DX_Context
{
	ID3D11DeviceContext4* imm_context;

	~DX_Context()
	{
		if (imm_context)
			imm_context->Release();
	}
};

bool dx_init_resources(DX_Machine* dxr, DX_Context* dxc, HWND win_handle)
{
	HRESULT hr;
	u32 device_flags = 0;
#ifdef _DEBUG
	device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	// Device and context creation
	{
		ID3D11Device* temp_device{};
		ID3D11DeviceContext* temp_imm_context{};

		D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1 };
		hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, device_flags, levels, ARRAYSIZE(levels),
		                       D3D11_SDK_VERSION, &temp_device, nullptr, &temp_imm_context);
		AssertHR(hr);

		hr = temp_device->QueryInterface(__uuidof(ID3D11Device5), (void**)&dxr->device);
		AssertHR(hr);
		hr = temp_imm_context->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&dxc->imm_context);
		AssertHR(hr);

		temp_device->Release();
		temp_imm_context->Release();
	}

#ifdef _DEBUG
	{
		// DXGI debug interface for broader error catching
		{
			IDXGIDevice4* dxgi_device{};
			hr = dxr->device->QueryInterface(__uuidof(IDXGIDevice4), (void**)&dxgi_device);
			AssertHR(hr);

			IDXGIInfoQueue* info_queue{};
			dxgi_device->QueryInterface(__uuidof(IDXGIInfoQueue), (void**)&info_queue);
			if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&info_queue))))
			{
				info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
				info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
			}
			info_queue->Release();
		}
		// Setup debug break on API errors - no need to manually check "HRESULT" - doesnt work?
		{
			ID3D11Debug* dx_debug{};
			hr = dxr->device->QueryInterface(__uuidof(ID3D11Debug), (void**)&dx_debug);
			AssertHR(hr);

			ID3D11InfoQueue* dx_info_queue{};
			if (SUCCEEDED(dx_debug->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&dx_info_queue)))
			{
				dx_info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
				dx_info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);

				D3D11_MESSAGE_ID hide[] =
				{
				D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,
				};
				D3D11_INFO_QUEUE_FILTER filter = {};
				filter.DenyList.NumIDs = _countof(hide);
				filter.DenyList.pIDList = hide;
				dx_info_queue->AddStorageFilterEntries(&filter);

				dx_info_queue->Release();
			}
			dx_debug->Release();
		}
	}
#endif

	auto&& [w, h] = Win32::get_window_client_dims(win_handle);

	// Creation of swap chain  -- BELOW SETTINGS ARE FOR CPU RASTERIZER ONLY!
	{
		IDXGIFactory5* dxgi_factory{};
		hr = CreateDXGIFactory2(device_flags, __uuidof(IDXGIFactory5), (void**)&dxgi_factory);
		AssertHR(hr);

		DXGI_SWAP_CHAIN_DESC1 desc_swap_chain
		{
			.Width  =  w,
			.Height =  h,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.SampleDesc = { 1, 0 },
			.BufferUsage = DXGI_USAGE_BACK_BUFFER,
			.BufferCount = 2, // For rasterizer 1 seems also to be fine?
			.Scaling = DXGI_SCALING_STRETCH,
			.SwapEffect = DXGI_SWAP_EFFECT_DISCARD
		};

		IDXGISwapChain1* temp_swap_chain{};
		hr = dxgi_factory->CreateSwapChainForHwnd(dxr->device, win_handle, &desc_swap_chain, 0, 0, &temp_swap_chain);
		AssertHR(hr);
		temp_swap_chain->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&dxr->swap_chain);

		dxgi_factory->MakeWindowAssociation(win_handle, DXGI_MWA_NO_ALT_ENTER); // Disable alt+enter changing monitor resolution
		temp_swap_chain->Release();
		dxgi_factory->Release();
	}
	
	return true;
}