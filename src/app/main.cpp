#include <gpu/RenderContext.h>
#include <gpu/ShaderProgram.h>
#include <gpu/Buffer.h>

#include <Windows.h>

#include "Local.h"

using namespace PixelMachine::GPU;

bool windowActive = true;

static LRESULT WinProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {

	switch (Msg)
	{
	case WM_CREATE:
		break;

	case WM_PAINT:
		break;

	case WM_SIZING:
		break;

	case WM_DESTROY:
		break;

	case WM_ENTERSIZEMOVE:
		break;

	case WM_EXITSIZEMOVE:
		break;

	case WM_CLOSE:
		windowActive = false;
		break;

	default:
		return DefWindowProcW(hWnd, Msg, wParam, lParam);
	}
}

static HWND CreateSystemWindow(const wchar_t *name , const int windowWidth, const int windowHeight) {

	HINSTANCE hInstance = GetModuleHandle(nullptr);
	WNDCLASSEXW wndClassExW = { 0u };
	const wchar_t windowClassName[] = L"DefaultWindowClass";

	wndClassExW.lpszClassName = windowClassName;
	wndClassExW.lpfnWndProc = WinProc;
	wndClassExW.hInstance = hInstance;
	wndClassExW.hCursor = NULL;
	wndClassExW.hIcon = NULL;
	wndClassExW.hbrBackground = (HBRUSH)(COLOR_BTNTEXT);
	wndClassExW.lpszMenuName = NULL;
	wndClassExW.hIconSm = NULL;
	wndClassExW.style = NULL;
	wndClassExW.cbClsExtra = NULL;
	wndClassExW.cbWndExtra = NULL;
	wndClassExW.cbSize = sizeof(wndClassExW);

	RegisterClassExW(&wndClassExW);

	RECT clientArea = { 0 };
	clientArea.top = 0u;
	clientArea.left = 0u;
	clientArea.bottom = windowHeight;
	clientArea.right = windowWidth;

	DWORD windowStyle = WS_SYSMENU;

	AdjustWindowRectEx(&clientArea, windowStyle, TRUE, WS_EX_OVERLAPPEDWINDOW);

	HWND systemWindowHandle = CreateWindowExW(
		WS_EX_OVERLAPPEDWINDOW,
		windowClassName,
		name,
		windowStyle,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowWidth,
		windowHeight,
		NULL,
		NULL,
		hInstance,
		NULL);

	return systemWindowHandle;
}

int main() {

	HWND windowHandle = CreateSystemWindow(L"PixelMachine", 1280, 720);
	ShowWindow(windowHandle, SW_SHOW);

	RenderContext::Initialize(windowHandle);
	RenderContext *pContext = RenderContext::Get();

	ShaderProgram *vertexShaderProgram = ShaderProgram::CreateFromCompiled("VS", VS_PATH , ShaderProgramType::VertexShader);
	ShaderProgram *fragShaderProgram = ShaderProgram::CreateFromCompiled("FS", FS_PATH, ShaderProgramType::FragmentShader);

	Buffer *vertexBuffer = Buffer::Create(
		BufferType::VertexBuffer,
		ShaderProgramType::VertexShader,
		BufferLayout({
		{ BufferDataType::float3, "position" },
		{ BufferDataType::float3, "color" } }),
		4 );

	const float vertexData[] = {
	   -0.25,-0.25, 0.0, // pos
		1.0, 0.5, 0.5, // color

		0.25, -0.25, 0.0, // pos
		0.1, 1.0, 0.4, // color

		0.25, 0.25, 0.0, // pos
		0.0, 0.0, 1.0,  // color

	   -0.25, 0.25, 0.0, // pos
		0.0, 1.0, 1.0  // color
	};

	vertexBuffer->SetData(vertexData);

	Buffer *indexBuffer = Buffer::Create(
		BufferType::IndexBuffer,
		ShaderProgramType::VertexShader,
		BufferLayout({ { BufferDataType::uint1, "index" } }),
		6);

	const uint32_t indexData[] = { 1u, 3u ,0u, 2u, 3u, 1u };

	indexBuffer->SetData(indexData);

	pContext->BeginPass();
	vertexShaderProgram->Bind();
	vertexBuffer->Bind();
	indexBuffer->Bind();
	fragShaderProgram->Bind();
	pContext->EndPass();

	while (windowActive) {
		MSG msg = { 0 };
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		pContext->RunPass(0);
		pContext->PresentFrame();
	}

	delete indexBuffer;
	delete vertexBuffer;
	delete vertexShaderProgram;
	delete fragShaderProgram;
	RenderContext::Destroy();

	return 0;
}