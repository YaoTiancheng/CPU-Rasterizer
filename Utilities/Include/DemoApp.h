#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wrl/client.h>
#include <d2d1.h>
#include "Rasterizer.h"

class CDemoApp
{
    friend LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );
public:
	CDemoApp( const wchar_t* name, HINSTANCE hInstance, uint32_t width, uint32_t height );

	virtual ~CDemoApp();

	bool Initialize();

	void Destroy();

	int Execute();

private:
	virtual bool OnInit() { return true; }
	virtual void OnDestroy() {}
	virtual void OnUpdate() {}
	virtual bool OnWndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam ) { return false; }

protected:
	void GetSwapChainSize( uint32_t* width, uint32_t* height );
	bool CopyToSwapChain( Rasterizer::SImage image );

	HWND m_hWnd;
    Microsoft::WRL::ComPtr<ID2D1Factory> m_D2DFactory;
	Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_D2DRenderTarget;
	Microsoft::WRL::ComPtr<ID2D1Bitmap> m_D2DBitmap;
};