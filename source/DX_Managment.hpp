#pragma once

struct GPU_Resource
{
	ID3D12Resource* ptr;
	D3D12_RESOURCE_STATES state;
	D3D12_RESOURCE_DESC desc;
};

