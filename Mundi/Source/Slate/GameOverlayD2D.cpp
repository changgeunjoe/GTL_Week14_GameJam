#include "pch.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <cmath>
#include <Windows.h>

#include "GameOverlayD2D.h"
#include "UIManager.h"
#include "World.h"
#include "GameModeBase.h"
#include "GameState.h"
#include "GameEngine.h"
#include "D3D11RHI.h"
#include "FViewport.h"
#include "Source/Runtime/Core/Misc/PathUtils.h"
#include "PlayerCharacter.h"
#include "PlayerController.h"
#include "BossEnemy.h"
#include "ObjectIterator.h"
#include "Source/Runtime/Game/Combat/StatsComponent.h"
#include "Source/Runtime/Engine/Components/SkeletalMeshComponent.h"
#include "Source/Runtime/Engine/Animation/AnimInstance.h"
#include "Source/Runtime/Engine/Animation/AnimMontage.h"

#ifdef _EDITOR
#include "USlateManager.h"
#include "Source/Slate/Windows/SViewportWindow.h"
#endif

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "windowscodecs")

// Custom font settings
static const wchar_t* CUSTOM_FONT_NAME = L"Mantinia";

static inline void SafeRelease(IUnknown* p) { if (p) p->Release(); }

UGameOverlayD2D& UGameOverlayD2D::Get()
{
    static UGameOverlayD2D Instance;
    return Instance;
}

void UGameOverlayD2D::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, IDXGISwapChain* InSwapChain)
{
    OutputDebugStringA("[GameOverlayD2D] Initialize called\n");

    D3DDevice = InDevice;
    D3DContext = InContext;
    SwapChain = InSwapChain;
    bInitialized = (D3DDevice && D3DContext && SwapChain);

    if (!bInitialized)
    {
        return;
    }

    D2D1_FACTORY_OPTIONS FactoryOpts{};
#ifdef _DEBUG
    FactoryOpts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &FactoryOpts, (void**)&D2DFactory)))
    {
        return;
    }

    IDXGIDevice* DxgiDevice = nullptr;
    if (FAILED(D3DDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&DxgiDevice)))
    {
        return;
    }

    if (FAILED(D2DFactory->CreateDevice(DxgiDevice, &D2DDevice)))
    {
        SafeRelease(DxgiDevice);
        return;
    }
    SafeRelease(DxgiDevice);

    if (FAILED(D2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &D2DContext)))
    {
        return;
    }

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&DWriteFactory)))
    {
        return;
    }

    // Initialize WIC factory for image loading
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&WICFactory))))
    {
        // Continue without WIC - images won't work but text will
    }

    // Load custom font from file (build full path using GDataDir)
    // Note: Using 0 instead of FR_PRIVATE so DirectWrite can see the font
    FWideString FontPath = UTF8ToWide(GDataDir) + L"/UI/Fonts/Mantinia Regular.otf";
    int FontsAdded = AddFontResourceExW(FontPath.c_str(), 0, 0);
    bFontLoaded = (FontsAdded > 0);

    // Use custom font if loaded, fallback to Segoe UI
    const wchar_t* FontName = bFontLoaded ? CUSTOM_FONT_NAME : L"Segoe UI";

    if (DWriteFactory)
    {
        // Title format - large centered text for start menu
        DWriteFactory->CreateTextFormat(
            FontName,
            nullptr,
            DWRITE_FONT_WEIGHT_REGULAR,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            200.0f,
            L"en-us",
            &TitleFormat);

        if (TitleFormat)
        {
            TitleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            TitleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        // Subtitle format - smaller centered text
        DWriteFactory->CreateTextFormat(
            FontName,
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            35.0f,
            L"en-us",
            &SubtitleFormat);

        if (SubtitleFormat)
        {
            SubtitleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            SubtitleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        // Death/Victory text format - large, dramatic
        DWriteFactory->CreateTextFormat(
            FontName,
            nullptr,
            DWRITE_FONT_WEIGHT_REGULAR,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            96.0f,
            L"en-us",
            &DeathTextFormat);

        if (DeathTextFormat)
        {
            DeathTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            DeathTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        // Boss name text format - above health bar
        DWriteFactory->CreateTextFormat(
            FontName,
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            28.0f,
            L"en-us",
            &BossNameFormat);

        if (BossNameFormat)
        {
            BossNameFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            BossNameFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        // Debug text format - small monospace for stats display
        DWriteFactory->CreateTextFormat(
            L"Consolas",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            14.0f,
            L"en-us",
            &DebugTextFormat);

        if (DebugTextFormat)
        {
            DebugTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            DebugTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    }

    // Load logo image
    if (WICFactory && D2DContext)
    {
        FWideString LogoPath = UTF8ToWide(GDataDir) + L"/UI/Icons/JeldenJingLogo.png";

        IWICBitmapDecoder* Decoder = nullptr;
        if (SUCCEEDED(WICFactory->CreateDecoderFromFilename(LogoPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &Decoder)))
        {
            IWICBitmapFrameDecode* Frame = nullptr;
            if (SUCCEEDED(Decoder->GetFrame(0, &Frame)))
            {
                IWICFormatConverter* Converter = nullptr;
                if (SUCCEEDED(WICFactory->CreateFormatConverter(&Converter)))
                {
                    if (SUCCEEDED(Converter->Initialize(Frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut)))
                    {
                        if (SUCCEEDED(D2DContext->CreateBitmapFromWicBitmap(Converter, nullptr, &LogoBitmap)))
                        {
                            D2D1_SIZE_F Size = LogoBitmap->GetSize();
                            LogoWidth = Size.width;
                            LogoHeight = Size.height;
                        }
                    }
                    SafeRelease(Converter);
                }
                SafeRelease(Frame);
            }
            SafeRelease(Decoder);
        }
    }

    // Load boss health bar frame image
    if (WICFactory && D2DContext)
    {
        FWideString FramePath = UTF8ToWide(GDataDir) + L"/UI/Icons/TX_Bar_Frame_Enemy.PNG";

        IWICBitmapDecoder* Decoder = nullptr;
        if (SUCCEEDED(WICFactory->CreateDecoderFromFilename(FramePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &Decoder)))
        {
            IWICBitmapFrameDecode* Frame = nullptr;
            if (SUCCEEDED(Decoder->GetFrame(0, &Frame)))
            {
                IWICFormatConverter* Converter = nullptr;
                if (SUCCEEDED(WICFactory->CreateFormatConverter(&Converter)))
                {
                    if (SUCCEEDED(Converter->Initialize(Frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut)))
                    {
                        if (SUCCEEDED(D2DContext->CreateBitmapFromWicBitmap(Converter, nullptr, &BossFrameBitmap)))
                        {
                            D2D1_SIZE_F Size = BossFrameBitmap->GetSize();
                            BossFrameWidth = Size.width;
                            BossFrameHeight = Size.height;
                        }
                    }
                    SafeRelease(Converter);
                }
                SafeRelease(Frame);
            }
            SafeRelease(Decoder);
        }
    }

    // Load boss health bar fill image (red)
    if (WICFactory && D2DContext)
    {
        FWideString BarPath = UTF8ToWide(GDataDir) + L"/UI/Icons/TX_Gauge_EnemyHP_Bar1.PNG";

        IWICBitmapDecoder* Decoder = nullptr;
        if (SUCCEEDED(WICFactory->CreateDecoderFromFilename(BarPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &Decoder)))
        {
            IWICBitmapFrameDecode* Frame = nullptr;
            if (SUCCEEDED(Decoder->GetFrame(0, &Frame)))
            {
                IWICFormatConverter* Converter = nullptr;
                if (SUCCEEDED(WICFactory->CreateFormatConverter(&Converter)))
                {
                    if (SUCCEEDED(Converter->Initialize(Frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut)))
                    {
                        if (SUCCEEDED(D2DContext->CreateBitmapFromWicBitmap(Converter, nullptr, &BossBarBitmap)))
                        {
                            D2D1_SIZE_F Size = BossBarBitmap->GetSize();
                            BossBarWidth = Size.width;
                            BossBarHeight = Size.height;
                        }
                    }
                    SafeRelease(Converter);
                }
                SafeRelease(Frame);
            }
            SafeRelease(Decoder);
        }
    }

    // Load boss health bar delayed damage image (yellow)
    if (WICFactory && D2DContext)
    {
        FWideString BarPath = UTF8ToWide(GDataDir) + L"/UI/Icons/TX_Gauge_EnemyHP_Bar_yellow.png";

        IWICBitmapDecoder* Decoder = nullptr;
        if (SUCCEEDED(WICFactory->CreateDecoderFromFilename(BarPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &Decoder)))
        {
            IWICBitmapFrameDecode* Frame = nullptr;
            if (SUCCEEDED(Decoder->GetFrame(0, &Frame)))
            {
                IWICFormatConverter* Converter = nullptr;
                if (SUCCEEDED(WICFactory->CreateFormatConverter(&Converter)))
                {
                    if (SUCCEEDED(Converter->Initialize(Frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut)))
                    {
                        D2DContext->CreateBitmapFromWicBitmap(Converter, nullptr, &BossBarYellowBitmap);
                    }
                    SafeRelease(Converter);
                }
                SafeRelease(Frame);
            }
            SafeRelease(Decoder);
        }
    }

    // Load player bar frame image
    if (WICFactory && D2DContext)
    {
        FWideString FramePath = UTF8ToWide(GDataDir) + L"/UI/Icons/TX_Bar_Frame_M.png";

        IWICBitmapDecoder* Decoder = nullptr;
        if (SUCCEEDED(WICFactory->CreateDecoderFromFilename(FramePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &Decoder)))
        {
            IWICBitmapFrameDecode* Frame = nullptr;
            if (SUCCEEDED(Decoder->GetFrame(0, &Frame)))
            {
                IWICFormatConverter* Converter = nullptr;
                if (SUCCEEDED(WICFactory->CreateFormatConverter(&Converter)))
                {
                    if (SUCCEEDED(Converter->Initialize(Frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut)))
                    {
                        if (SUCCEEDED(D2DContext->CreateBitmapFromWicBitmap(Converter, nullptr, &PlayerFrameBitmap)))
                        {
                            D2D1_SIZE_F Size = PlayerFrameBitmap->GetSize();
                            PlayerFrameWidth = Size.width;
                            PlayerFrameHeight = Size.height;
                        }
                    }
                    SafeRelease(Converter);
                }
                SafeRelease(Frame);
            }
            SafeRelease(Decoder);
        }
    }

    // Load player HP bar (red)
    if (WICFactory && D2DContext)
    {
        FWideString BarPath = UTF8ToWide(GDataDir) + L"/UI/Icons/TX_Gauge_HP_Bar_red.png";

        IWICBitmapDecoder* Decoder = nullptr;
        if (SUCCEEDED(WICFactory->CreateDecoderFromFilename(BarPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &Decoder)))
        {
            IWICBitmapFrameDecode* Frame = nullptr;
            if (SUCCEEDED(Decoder->GetFrame(0, &Frame)))
            {
                IWICFormatConverter* Converter = nullptr;
                if (SUCCEEDED(WICFactory->CreateFormatConverter(&Converter)))
                {
                    if (SUCCEEDED(Converter->Initialize(Frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut)))
                    {
                        if (SUCCEEDED(D2DContext->CreateBitmapFromWicBitmap(Converter, nullptr, &PlayerHPBarBitmap)))
                        {
                            D2D1_SIZE_F Size = PlayerHPBarBitmap->GetSize();
                            PlayerBarWidth = Size.width;
                            PlayerBarHeight = Size.height;
                        }
                    }
                    SafeRelease(Converter);
                }
                SafeRelease(Frame);
            }
            SafeRelease(Decoder);
        }
    }

    // Load player Focus bar
    if (WICFactory && D2DContext)
    {
        FWideString BarPath = UTF8ToWide(GDataDir) + L"/UI/Icons/TX_Gauge_Stamina_Bar.png";

        IWICBitmapDecoder* Decoder = nullptr;
        if (SUCCEEDED(WICFactory->CreateDecoderFromFilename(BarPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &Decoder)))
        {
            IWICBitmapFrameDecode* Frame = nullptr;
            if (SUCCEEDED(Decoder->GetFrame(0, &Frame)))
            {
                IWICFormatConverter* Converter = nullptr;
                if (SUCCEEDED(WICFactory->CreateFormatConverter(&Converter)))
                {
                    if (SUCCEEDED(Converter->Initialize(Frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut)))
                    {
                        D2DContext->CreateBitmapFromWicBitmap(Converter, nullptr, &PlayerFocusBarBitmap);
                    }
                    SafeRelease(Converter);
                }
                SafeRelease(Frame);
            }
            SafeRelease(Decoder);
        }
    }

    // Load player Stamina bar
    if (WICFactory && D2DContext)
    {
        FWideString BarPath = UTF8ToWide(GDataDir) + L"/UI/Icons/TX_Gauge_Focus_Bar.png";

        IWICBitmapDecoder* Decoder = nullptr;
        if (SUCCEEDED(WICFactory->CreateDecoderFromFilename(BarPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &Decoder)))
        {
            IWICBitmapFrameDecode* Frame = nullptr;
            if (SUCCEEDED(Decoder->GetFrame(0, &Frame)))
            {
                IWICFormatConverter* Converter = nullptr;
                if (SUCCEEDED(WICFactory->CreateFormatConverter(&Converter)))
                {
                    if (SUCCEEDED(Converter->Initialize(Frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut)))
                    {
                        D2DContext->CreateBitmapFromWicBitmap(Converter, nullptr, &PlayerStaminaBarBitmap);
                    }
                    SafeRelease(Converter);
                }
                SafeRelease(Frame);
            }
            SafeRelease(Decoder);
        }
    }

    // Load player bar yellow (delayed damage indicator)
    if (WICFactory && D2DContext)
    {
        FWideString BarPath = UTF8ToWide(GDataDir) + L"/UI/Icons/TX_Gauge_Bar_yellow.png";

        IWICBitmapDecoder* Decoder = nullptr;
        if (SUCCEEDED(WICFactory->CreateDecoderFromFilename(BarPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &Decoder)))
        {
            IWICBitmapFrameDecode* Frame = nullptr;
            if (SUCCEEDED(Decoder->GetFrame(0, &Frame)))
            {
                IWICFormatConverter* Converter = nullptr;
                if (SUCCEEDED(WICFactory->CreateFormatConverter(&Converter)))
                {
                    if (SUCCEEDED(Converter->Initialize(Frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut)))
                    {
                        D2DContext->CreateBitmapFromWicBitmap(Converter, nullptr, &PlayerBarYellowBitmap);
                    }
                    SafeRelease(Converter);
                }
                SafeRelease(Frame);
            }
            SafeRelease(Decoder);
        }
    }

    EnsureInitialized();
}

void UGameOverlayD2D::Shutdown()
{
    ReleaseD2DResources();

    // Unload custom font
    if (bFontLoaded)
    {
        FWideString FontPath = UTF8ToWide(GDataDir) + L"/UI/Fonts/Mantinia Regular.otf";
        RemoveFontResourceExW(FontPath.c_str(), 0, 0);
        bFontLoaded = false;
    }

    D3DDevice = nullptr;
    D3DContext = nullptr;
    SwapChain = nullptr;
    bInitialized = false;
}

void UGameOverlayD2D::EnsureInitialized()
{
    if (!D2DContext)
    {
        return;
    }

    SafeRelease(TextBrush);
    SafeRelease(SubtitleBrush);
    SafeRelease(DeathTextBrush);
    SafeRelease(VictoryTextBrush);

    // Title text color: RGB(207, 185, 144)
    D2DContext->CreateSolidColorBrush(D2D1::ColorF(207.0f/255.0f, 185.0f/255.0f, 144.0f/255.0f, 1.0f), &TextBrush);

    // Subtitle text color: White
    D2DContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &SubtitleBrush);

    // Blood red for "YOU DIED"
    D2DContext->CreateSolidColorBrush(D2D1::ColorF(0.6f, 0.0f, 0.0f, 1.0f), &DeathTextBrush);

    // Golden for "DEMIGOD FELLED"
    D2DContext->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.65f, 0.13f, 1.0f), &VictoryTextBrush);
}

void UGameOverlayD2D::ReleaseD2DResources()
{
    SafeRelease(LogoBitmap);
    SafeRelease(BossFrameBitmap);
    SafeRelease(BossBarBitmap);
    SafeRelease(BossBarYellowBitmap);
    SafeRelease(PlayerFrameBitmap);
    SafeRelease(PlayerHPBarBitmap);
    SafeRelease(PlayerFocusBarBitmap);
    SafeRelease(PlayerStaminaBarBitmap);
    SafeRelease(PlayerBarYellowBitmap);
    SafeRelease(TextBrush);
    SafeRelease(SubtitleBrush);
    SafeRelease(DeathTextBrush);
    SafeRelease(VictoryTextBrush);
    SafeRelease(DebugTextFormat);
    SafeRelease(BossNameFormat);
    SafeRelease(DeathTextFormat);
    SafeRelease(SubtitleFormat);
    SafeRelease(TitleFormat);
    SafeRelease(DWriteFactory);
    SafeRelease(WICFactory);
    SafeRelease(D2DContext);
    SafeRelease(D2DDevice);
    SafeRelease(D2DFactory);
}

// ============================================================================
// Gradient Brush Creation
// ============================================================================

ID2D1LinearGradientBrush* UGameOverlayD2D::CreateBannerGradientBrush(float ScreenW, float ScreenH, float Opacity)
{
    if (!D2DContext)
    {
        return nullptr;
    }

    // Banner height (about 15% of screen height)
    float BannerHeight = ScreenH * 0.15f;
    float BannerTop = (ScreenH - BannerHeight) * 0.5f;
    float BannerBottom = BannerTop + BannerHeight;

    // Gradient stops: transparent -> black -> black -> transparent (vertical gradient)
    D2D1_GRADIENT_STOP GradientStops[4];
    GradientStops[0].position = 0.0f;
    GradientStops[0].color = D2D1::ColorF(0, 0, 0, 0);  // Transparent at top edge
    GradientStops[1].position = 0.3f;
    GradientStops[1].color = D2D1::ColorF(0, 0, 0, 0.85f * Opacity);  // Fade to black
    GradientStops[2].position = 0.7f;
    GradientStops[2].color = D2D1::ColorF(0, 0, 0, 0.85f * Opacity);  // Hold black
    GradientStops[3].position = 1.0f;
    GradientStops[3].color = D2D1::ColorF(0, 0, 0, 0);  // Transparent at bottom edge

    ID2D1GradientStopCollection* GradientStopCollection = nullptr;
    HRESULT hr = D2DContext->CreateGradientStopCollection(
        GradientStops,
        4,
        D2D1_GAMMA_2_2,
        D2D1_EXTEND_MODE_CLAMP,
        &GradientStopCollection);

    if (FAILED(hr))
    {
        return nullptr;
    }

    ID2D1LinearGradientBrush* GradientBrush = nullptr;
    hr = D2DContext->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, BannerTop),
            D2D1::Point2F(0, BannerBottom)),
        GradientStopCollection,
        &GradientBrush);

    SafeRelease(GradientStopCollection);

    return GradientBrush;
}

// ============================================================================
// Draw Functions
// ============================================================================

void UGameOverlayD2D::DrawStartMenu(float ScreenW, float ScreenH)
{
    // Draw logo in background (centered, match height of screen)
    if (LogoBitmap && LogoWidth > 0 && LogoHeight > 0)
    {
        // Target size: 75% of screen height, maintain aspect ratio
        float TargetH = ScreenH;
        float Scale = TargetH / LogoHeight;
        float ScaledW = LogoWidth * Scale;
        float ScaledH = TargetH;

        // Center the logo
        float DrawX = (ScreenW - ScaledW) * 0.5f;
        float DrawY = (ScreenH - ScaledH) * 0.5f;

        D2D1_RECT_F LogoRect = D2D1::RectF(DrawX, DrawY, DrawX + ScaledW, DrawY + ScaledH);
        D2DContext->DrawBitmap(LogoBitmap, LogoRect, 1.0f);
    }

    TextBrush->SetOpacity(1.0f);

    // Title text - centered, upper portion of screen
    const wchar_t* TitleText = L"Jelden Jing";
    D2D1_RECT_F TitleRect = D2D1::RectF(0, ScreenH * 0.35f, ScreenW, ScreenH * 0.50f);
    D2DContext->DrawTextW(
        TitleText,
        static_cast<UINT32>(wcslen(TitleText)),
        TitleFormat,
        TitleRect,
        TextBrush);

    // Subtitle text - centered, below title
    const wchar_t* SubtitleText = L"Press any key to start";
    D2D1_RECT_F SubtitleRect = D2D1::RectF(0, ScreenH * 0.85f, ScreenW, ScreenH * 0.65f);
    D2DContext->DrawTextW(
        SubtitleText,
        static_cast<UINT32>(wcslen(SubtitleText)),
        SubtitleFormat,
        SubtitleRect,
        SubtitleBrush);
}

void UGameOverlayD2D::DrawDeathScreen(float ScreenW, float ScreenH, const wchar_t* Text, bool bIsVictory)
{
    float DeltaTime = UUIManager::GetInstance().GetDeltaTime();
    DeathScreenTimer += DeltaTime;

    // Calculate fade animation
    float Opacity = 0.0f;
    float TotalDuration = DeathFadeInDuration + DeathHoldDuration + DeathFadeOutDuration;

    if (DeathScreenTimer < DeathFadeInDuration)
    {
        // Fade in phase
        Opacity = DeathScreenTimer / DeathFadeInDuration;
    }
    else if (DeathScreenTimer < DeathFadeInDuration + DeathHoldDuration)
    {
        // Hold phase
        Opacity = 1.0f;
    }
    else if (DeathScreenTimer < TotalDuration)
    {
        // Fade out phase
        float FadeOutProgress = (DeathScreenTimer - DeathFadeInDuration - DeathHoldDuration) / DeathFadeOutDuration;
        Opacity = 1.0f - FadeOutProgress;
    }
    else
    {
        // Animation complete - still show at 0 opacity (or could skip drawing)
        Opacity = 0.0f;
    }

    if (Opacity <= 0.0f)
    {
        return;
    }

    // Clamp opacity
    Opacity = (Opacity > 1.0f) ? 1.0f : ((Opacity < 0.0f) ? 0.0f : Opacity);

    // Draw the gradient banner
    ID2D1LinearGradientBrush* BannerBrush = CreateBannerGradientBrush(ScreenW, ScreenH, Opacity);
    if (BannerBrush)
    {
        // Draw full-width banner at center of screen
        float BannerHeight = ScreenH * 0.15f;
        float BannerTop = (ScreenH - BannerHeight) * 0.5f;
        D2D1_RECT_F BannerRect = D2D1::RectF(0, BannerTop, ScreenW, BannerTop + BannerHeight);
        D2DContext->FillRectangle(BannerRect, BannerBrush);
        SafeRelease(BannerBrush);
    }

    // Select the appropriate colored brush
    ID2D1SolidColorBrush* ColoredBrush = bIsVictory ? VictoryTextBrush : DeathTextBrush;
    if (ColoredBrush)
    {
        ColoredBrush->SetOpacity(Opacity);
    }

    // Draw the text on top of the banner
    D2D1_RECT_F TextRect = D2D1::RectF(0, ScreenH * 0.425f, ScreenW, ScreenH * 0.575f);
    D2DContext->DrawTextW(
        Text,
        static_cast<UINT32>(wcslen(Text)),
        DeathTextFormat,
        TextRect,
        ColoredBrush ? ColoredBrush : TextBrush);
}

void UGameOverlayD2D::DrawBossHealthBar(float ScreenW, float ScreenH, float DeltaTime)
{
    // Check if we have valid boss bar resources
    if (!BossFrameBitmap || !BossBarBitmap || !BossNameFormat)
    {
        return;
    }

    // Get boss health from GameState
    if (!GWorld)
    {
        return;
    }

    AGameModeBase* GM = GWorld->GetGameMode();
    if (!GM)
    {
        return;
    }

    AGameState* GS = Cast<AGameState>(GM->GetGameState());
    if (!GS)
    {
        return;
    }

    // Get target health - default to 100% if no boss registered
    float TargetHealth = GS->HasActiveBoss() ? GS->GetBossHealth().GetPercent() : 1.0f;

    // ========== Fade-in Animation ==========
    if (!bBossBarFadingIn && BossBarOpacity < 1.0f)
    {
        bBossBarFadingIn = true;
        BossBarFadeTimer = 0.0f;
    }

    if (bBossBarFadingIn)
    {
        BossBarFadeTimer += DeltaTime;
        BossBarOpacity = BossBarFadeTimer / BossBarFadeDuration;
        if (BossBarOpacity >= 1.0f)
        {
            BossBarOpacity = 1.0f;
            bBossBarFadingIn = false;
        }
    }

    // Dark Souls style health bar animation:
    // - Red bar (CurrentBossHealth) snaps immediately to actual health
    // - Yellow bar (DelayedBossHealth) waits, then slowly catches up

    // Detect if damage was taken (health decreased)
    if (TargetHealth < CurrentBossHealth)
    {
        // Reset the delay timer when new damage is taken
        DelayedHealthTimer = 0.0f;
    }

    // Red bar snaps immediately
    CurrentBossHealth = TargetHealth;

    // Yellow bar logic: wait for delay, then lerp
    if (DelayedBossHealth > CurrentBossHealth)
    {
        DelayedHealthTimer += DeltaTime;

        // After delay, start lerping the yellow bar down
        if (DelayedHealthTimer >= DelayedHealthDelay)
        {
            float LerpAmount = DelayedHealthLerpSpeed * DeltaTime;
            DelayedBossHealth -= LerpAmount;
            if (DelayedBossHealth < CurrentBossHealth)
            {
                DelayedBossHealth = CurrentBossHealth;
            }
        }
    }
    else
    {
        // If healing or initialization, sync yellow bar immediately
        DelayedBossHealth = CurrentBossHealth;
        DelayedHealthTimer = 0.0f;
    }

    // Calculate bar position (bottom center, 50% of viewport width)
    float BarW = ScreenW * 0.5f;
    float BarScale = BarW / BossFrameWidth;  // Scale to fit 50% of screen width
    float BarH = BossFrameHeight * BarScale;
    float BarX = (ScreenW - BarW) * 0.5f;
    float BarY = (ScreenH - BarH) * 0.85f;

    // 1. Draw frame first (background)
    D2D1_RECT_F FrameRect = D2D1::RectF(BarX, BarY, BarX + BarW, BarY + BarH);
    D2DContext->DrawBitmap(BossFrameBitmap, FrameRect, BossBarOpacity);

    // 2. Draw yellow bar (delayed damage indicator) - behind red bar
    if (BossBarYellowBitmap && DelayedBossHealth > 0.0f && DelayedBossHealth > CurrentBossHealth)
    {
        float YellowFillW = BossBarWidth * BarScale * DelayedBossHealth;
        D2D1_RECT_F YellowDestRect = D2D1::RectF(BarX, BarY, BarX + YellowFillW, BarY + BarH);
        D2D1_RECT_F YellowSrcRect = D2D1::RectF(0, 0, BossBarWidth * DelayedBossHealth, BossBarHeight);

        D2DContext->DrawBitmap(BossBarYellowBitmap, YellowDestRect, BossBarOpacity,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &YellowSrcRect);
    }

    // 3. Draw red bar on top (current health - snaps immediately)
    if (CurrentBossHealth > 0.0f)
    {
        float FillW = BossBarWidth * BarScale * CurrentBossHealth;
        D2D1_RECT_F FillDestRect = D2D1::RectF(BarX, BarY, BarX + FillW, BarY + BarH);
        D2D1_RECT_F FillSrcRect = D2D1::RectF(0, 0, BossBarWidth * CurrentBossHealth, BossBarHeight);

        D2DContext->DrawBitmap(BossBarBitmap, FillDestRect, BossBarOpacity,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &FillSrcRect);
    }

    // 4. Draw boss name above bar (only if boss is registered)
    if (GS->HasActiveBoss())
    {
        SubtitleBrush->SetOpacity(BossBarOpacity);
        FWideString BossName = UTF8ToWide(GS->GetBossName());
        D2D1_RECT_F NameRect = D2D1::RectF(BarX, BarY - 40.0f, BarX + BarW, BarY);
        D2DContext->DrawTextW(BossName.c_str(), static_cast<UINT32>(BossName.length()),
            BossNameFormat, NameRect, SubtitleBrush);
        SubtitleBrush->SetOpacity(1.0f);  // Reset opacity
    }
}

void UGameOverlayD2D::DrawPlayerBars(float ScreenW, float ScreenH, float DeltaTime)
{
    // Check if we have valid player bar resources
    if (!PlayerFrameBitmap || !PlayerHPBarBitmap || PlayerFrameWidth <= 0.f)
    {
        return;
    }

    // Get player stats from GameState
    if (!GWorld)
    {
        return;
    }

    AGameModeBase* GM = GWorld->GetGameMode();
    if (!GM)
    {
        return;
    }

    AGameState* GS = Cast<AGameState>(GM->GetGameState());
    if (!GS)
    {
        return;
    }

    // Get target values from GameState
    float TargetHP = GS->GetPlayerHealth().GetPercent();
    float TargetFocus = GS->GetPlayerFocus().GetPercent();
    float TargetStamina = GS->GetPlayerStamina().GetPercent();

    // Default to full if not initialized (Focus starts at 0)
    if (GS->GetPlayerHealth().Max <= 0.0f) TargetHP = 1.0f;
    if (GS->GetPlayerFocus().Max <= 0.0f) TargetFocus = 0.0f;
    if (GS->GetPlayerStamina().Max <= 0.0f) TargetStamina = 1.0f;

    // ========== Fade-in Animation ==========
    // Start fade-in when bars first appear
    if (!bPlayerBarsFadingIn && PlayerBarOpacity < 1.0f)
    {
        bPlayerBarsFadingIn = true;
        PlayerBarFadeTimer = 0.0f;
    }

    if (bPlayerBarsFadingIn)
    {
        PlayerBarFadeTimer += DeltaTime;
        PlayerBarOpacity = PlayerBarFadeTimer / PlayerBarFadeDuration;
        if (PlayerBarOpacity >= 1.0f)
        {
            PlayerBarOpacity = 1.0f;
            bPlayerBarsFadingIn = false;
        }
    }

    // ========== HP Bar Animation ==========
    if (TargetHP < CurrentPlayerHP)
    {
        DelayedPlayerHPTimer = 0.0f;
    }
    CurrentPlayerHP = TargetHP;

    if (DelayedPlayerHP > CurrentPlayerHP)
    {
        DelayedPlayerHPTimer += DeltaTime;
        if (DelayedPlayerHPTimer >= PlayerBarDelayTime)
        {
            DelayedPlayerHP -= PlayerBarLerpSpeed * DeltaTime;
            if (DelayedPlayerHP < CurrentPlayerHP)
            {
                DelayedPlayerHP = CurrentPlayerHP;
            }
        }
    }
    else
    {
        DelayedPlayerHP = CurrentPlayerHP;
        DelayedPlayerHPTimer = 0.0f;
    }

    // ========== Focus Bar Animation ==========
    if (TargetFocus < CurrentPlayerFocus)
    {
        DelayedPlayerFocusTimer = 0.0f;
    }
    CurrentPlayerFocus = TargetFocus;

    if (DelayedPlayerFocus > CurrentPlayerFocus)
    {
        DelayedPlayerFocusTimer += DeltaTime;
        if (DelayedPlayerFocusTimer >= PlayerBarDelayTime)
        {
            DelayedPlayerFocus -= PlayerBarLerpSpeed * DeltaTime;
            if (DelayedPlayerFocus < CurrentPlayerFocus)
            {
                DelayedPlayerFocus = CurrentPlayerFocus;
            }
        }
    }
    else
    {
        DelayedPlayerFocus = CurrentPlayerFocus;
        DelayedPlayerFocusTimer = 0.0f;
    }

    // ========== Stamina Bar Animation ==========
    if (TargetStamina < CurrentPlayerStamina)
    {
        DelayedPlayerStaminaTimer = 0.0f;
    }
    CurrentPlayerStamina = TargetStamina;

    if (DelayedPlayerStamina > CurrentPlayerStamina)
    {
        DelayedPlayerStaminaTimer += DeltaTime;
        if (DelayedPlayerStaminaTimer >= PlayerBarDelayTime)
        {
            DelayedPlayerStamina -= PlayerBarLerpSpeed * DeltaTime;
            if (DelayedPlayerStamina < CurrentPlayerStamina)
            {
                DelayedPlayerStamina = CurrentPlayerStamina;
            }
        }
    }
    else
    {
        DelayedPlayerStamina = CurrentPlayerStamina;
        DelayedPlayerStaminaTimer = 0.0f;
    }

    // ========== Calculate bar dimensions ==========
    // Position at top-left with some padding
    float Padding = 30.0f;
    float BarScale = 1.2f;  // Scale up the bars
    float BarW = PlayerFrameWidth * BarScale;
    float BarH = PlayerFrameHeight * BarScale;
    float BarSpacing = BarH * 1.0f;  // Vertical spacing between bars

    float BarX = Padding;
    float HPBarY = Padding + 50.0f;  // Move down a bit
    float FocusBarY = HPBarY + BarSpacing;
    float StaminaBarY = FocusBarY + BarSpacing;

    // Helper lambda to draw a single bar with Dark Souls style animation
    auto DrawSingleBar = [&](float BarY, float CurrentValue, float DelayedValue, ID2D1Bitmap* BarBitmap)
    {
        // 1. Draw frame
        D2D1_RECT_F FrameRect = D2D1::RectF(BarX, BarY, BarX + BarW, BarY + BarH);
        D2DContext->DrawBitmap(PlayerFrameBitmap, FrameRect, PlayerBarOpacity);

        // 2. Draw yellow bar (delayed) - behind colored bar
        if (PlayerBarYellowBitmap && DelayedValue > 0.0f && DelayedValue > CurrentValue)
        {
            float YellowFillW = PlayerBarWidth * BarScale * DelayedValue;
            D2D1_RECT_F YellowDestRect = D2D1::RectF(BarX, BarY, BarX + YellowFillW, BarY + BarH);
            D2D1_RECT_F YellowSrcRect = D2D1::RectF(0, 0, PlayerBarWidth * DelayedValue, PlayerBarHeight);
            D2DContext->DrawBitmap(PlayerBarYellowBitmap, YellowDestRect, PlayerBarOpacity,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &YellowSrcRect);
        }

        // 3. Draw colored bar on top
        if (BarBitmap && CurrentValue > 0.0f)
        {
            float FillW = PlayerBarWidth * BarScale * CurrentValue;
            D2D1_RECT_F FillDestRect = D2D1::RectF(BarX, BarY, BarX + FillW, BarY + BarH);
            D2D1_RECT_F FillSrcRect = D2D1::RectF(0, 0, PlayerBarWidth * CurrentValue, PlayerBarHeight);
            D2DContext->DrawBitmap(BarBitmap, FillDestRect, PlayerBarOpacity,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &FillSrcRect);
        }
    };

    // Draw the three bars: HP, Focus, Stamina
    DrawSingleBar(HPBarY, CurrentPlayerHP, DelayedPlayerHP, PlayerHPBarBitmap);
    DrawSingleBar(FocusBarY, CurrentPlayerFocus, DelayedPlayerFocus, PlayerFocusBarBitmap);
    DrawSingleBar(StaminaBarY, CurrentPlayerStamina, DelayedPlayerStamina, PlayerStaminaBarBitmap);
}

void UGameOverlayD2D::DrawDebugStats(float ScreenW, float ScreenH)
{
    if (!DebugTextFormat || !SubtitleBrush)
    {
        return;
    }

    if (!GWorld)
    {
        return;
    }

    // ========================================================================
    // 플레이어 상태 수집
    // ========================================================================
    FString PlayerStateStr = "Unknown";
    FString PlayerAttackStr = "None";
    float PlayerHP = 0.f;
    float PlayerMaxHP = 100.f;
    bool bPlayerBlocking = false;
    bool bPlayerInvincible = false;

    AGameModeBase* GameMode = GWorld->GetGameMode();
    if (GameMode && GameMode->PlayerController)
    {
        APawn* Pawn = GameMode->PlayerController->GetPawn();
        APlayerCharacter* Player = Cast<APlayerCharacter>(Pawn);
        if (Player)
        {
            ECombatState State = Player->GetCombatState();
            switch (State)
            {
            case ECombatState::Idle:      PlayerStateStr = "Idle"; break;
            case ECombatState::Attacking: PlayerStateStr = "Attacking"; break;
            case ECombatState::Dodging:   PlayerStateStr = "Dodging"; break;
            case ECombatState::Blocking:  PlayerStateStr = "Blocking"; break;
            case ECombatState::Parrying:  PlayerStateStr = "Parrying"; break;
            case ECombatState::Staggered: PlayerStateStr = "Staggered"; break;
            case ECombatState::Knockback: PlayerStateStr = "Knockback"; break;
            case ECombatState::Dead:      PlayerStateStr = "Dead"; break;
            default:                      PlayerStateStr = "Unknown"; break;
            }

            // 현재 무기 데미지 타입 (스킬) 가져오기
            EDamageType DmgType = Player->GetWeaponDamageInfo().DamageType;
            switch (DmgType)
            {
            case EDamageType::Light:          PlayerAttackStr = "Light"; break;
            case EDamageType::Heavy:          PlayerAttackStr = "Heavy"; break;
            case EDamageType::Special:        PlayerAttackStr = "Special"; break;
            case EDamageType::Parried:        PlayerAttackStr = "Parried"; break;
            case EDamageType::DashAttack:     PlayerAttackStr = "DashAttack"; break;
            case EDamageType::UltimateAttack: PlayerAttackStr = "UltimateAttack"; break;
            default:                          PlayerAttackStr = "Unknown"; break;
            }

            bPlayerBlocking = Player->IsBlocking();
            bPlayerInvincible = Player->IsInvincible();

            if (UStatsComponent* Stats = Player->GetStatsComponent())
            {
                PlayerHP = Stats->CurrentHealth;
                PlayerMaxHP = Stats->MaxHealth;
            }
        }
    }

    // ========================================================================
    // 보스 상태 수집
    // ========================================================================
    FString BossStateStr = "No Boss";
    FString BossAttackStr = "None";
    int32 BossPhase = 0;
    float BossHP = 0.f;
    float BossMaxHP = 100.f;
    bool bBossSuperArmor = false;

    // 월드의 모든 Actor에서 BossEnemy 찾기
    for (AActor* Actor : GWorld->GetActors())
    {
        ABossEnemy* Boss = Cast<ABossEnemy>(Actor);
        if (Boss && Boss->IsAlive())
        {
            BossPhase = Boss->GetPhase();

            // 보스 상태 (몽타주 재생 여부로 공격 중인지 판단)
            if (USkeletalMeshComponent* Mesh = Boss->GetMesh())
            {
                if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
                {
                    if (AnimInst->Montage_IsPlaying())
                    {
                        BossStateStr = "Attacking";
                        // 현재 재생 중인 몽타주 이름 가져오기
                        if (UAnimMontage* CurrentMontage = AnimInst->Montage_GetCurrentMontage())
                        {
                            BossAttackStr = CurrentMontage->GetName();
                        }
                        else
                        {
                            BossAttackStr = "Montage Playing";
                        }
                    }
                    else
                    {
                        BossStateStr = "Idle";
                        BossAttackStr = "None";
                    }
                }
            }

            bBossSuperArmor = Boss->HasSuperArmor();

            if (UStatsComponent* Stats = Boss->GetStatsComponent())
            {
                BossHP = Stats->CurrentHealth;
                BossMaxHP = Stats->MaxHealth;
            }
            break;  // 첫 번째 보스만
        }
    }

    // ========================================================================
    // 디버그 텍스트 그리기
    // ========================================================================
    SubtitleBrush->SetOpacity(0.9f);

    // 배경 반투명 박스
    ID2D1SolidColorBrush* BgBrush = nullptr;
    D2DContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.7f), &BgBrush);

    float BoxX = 10.f;
    float BoxY = 60.f;  // FPS 아래로 이동
    float BoxW = 280.f;
    float BoxH = 220.f;  // Attack 라인 추가로 높이 증가

    if (BgBrush)
    {
        D2D1_RECT_F BgRect = D2D1::RectF(BoxX, BoxY, BoxX + BoxW, BoxY + BoxH);
        D2DContext->FillRectangle(BgRect, BgBrush);
        SafeRelease(BgBrush);
    }

    // 텍스트 내용 구성
    wchar_t DebugText[1024];
    swprintf_s(DebugText, 1024,
        L"=== PLAYER ===\n"
        L"State: %hs\n"
        L"Attack: %hs\n"
        L"HP: %.0f / %.0f\n"
        L"Blocking: %hs\n"
        L"Invincible: %hs\n"
        L"\n"
        L"=== BOSS ===\n"
        L"Phase: %d\n"
        L"State: %hs\n"
        L"Attack: %hs\n"
        L"HP: %.0f / %.0f\n"
        L"SuperArmor: %hs",
        PlayerStateStr.c_str(),
        PlayerAttackStr.c_str(),
        PlayerHP, PlayerMaxHP,
        bPlayerBlocking ? "Yes" : "No",
        bPlayerInvincible ? "Yes" : "No",
        BossPhase,
        BossStateStr.c_str(),
        BossAttackStr.c_str(),
        BossHP, BossMaxHP,
        bBossSuperArmor ? "Yes" : "No"
    );

    D2D1_RECT_F TextRect = D2D1::RectF(BoxX + 10.f, BoxY + 5.f, BoxX + BoxW - 10.f, BoxY + BoxH - 5.f);
    D2DContext->DrawTextW(DebugText, static_cast<UINT32>(wcslen(DebugText)),
        DebugTextFormat, TextRect, SubtitleBrush);

    SubtitleBrush->SetOpacity(1.0f);
}

void UGameOverlayD2D::Draw()
{
    if (!bInitialized || !SwapChain || !D2DContext || !TitleFormat || !SubtitleFormat || !TextBrush)
    {
        return;
    }

    if (!GWorld)
    {
        return;
    }

    AGameModeBase* GM = GWorld->GetGameMode();
    if (!GM)
    {
        return;
    }

    AGameState* GS = Cast<AGameState>(GM->GetGameState());
    if (!GS)
    {
        return;
    }

    EGameFlowState FlowState = GS->GetGameFlowState();

    // Only draw for specific states
    bool bShouldDraw = (FlowState == EGameFlowState::StartMenu ||
                        FlowState == EGameFlowState::Fighting ||
                        FlowState == EGameFlowState::Victory ||
                        FlowState == EGameFlowState::Defeat);

    if (!bShouldDraw)
    {
        // Reset death screen timer when not in death/victory state
        DeathScreenTimer = 0.0f;
        return;
    }

    // Get viewport dimensions
    // In editor mode, use MainViewport from USlateManager (PIE viewport)
    // In game mode, use GEngine.GameViewport
    FViewport* Viewport = nullptr;

#ifdef _EDITOR
    SViewportWindow* MainViewportWindow = USlateManager::GetInstance().GetMainViewport();
    if (MainViewportWindow)
    {
        Viewport = MainViewportWindow->GetViewport();
    }
#else
    Viewport = GEngine.GameViewport.get();
#endif

    if (!Viewport)
    {
        return;
    }

    float ViewportX = static_cast<float>(Viewport->GetStartX());
    float ViewportY = static_cast<float>(Viewport->GetStartY());
    float ScreenW = static_cast<float>(Viewport->GetSizeX());
    float ScreenH = static_cast<float>(Viewport->GetSizeY());

    // Create D2D bitmap from backbuffer
    IDXGISurface* Surface = nullptr;
    if (FAILED(SwapChain->GetBuffer(0, __uuidof(IDXGISurface), (void**)&Surface)))
    {
        return;
    }

    D2D1_BITMAP_PROPERTIES1 BmpProps = {};
    BmpProps.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    BmpProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    BmpProps.dpiX = 96.0f;
    BmpProps.dpiY = 96.0f;
    BmpProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    ID2D1Bitmap1* TargetBmp = nullptr;
    if (FAILED(D2DContext->CreateBitmapFromDxgiSurface(Surface, &BmpProps, &TargetBmp)))
    {
        SafeRelease(Surface);
        return;
    }

    D2DContext->SetTarget(TargetBmp);
    D2DContext->BeginDraw();

    // Apply transform to offset drawing to viewport region
    D2DContext->SetTransform(D2D1::Matrix3x2F::Translation(ViewportX, ViewportY));

    // Set clip rect to viewport bounds
    D2DContext->PushAxisAlignedClip(
        D2D1::RectF(0, 0, ScreenW, ScreenH),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Get delta time for animations
    float DeltaTime = UUIManager::GetInstance().GetDeltaTime();

    // Reset bar fade when not fighting
    if (FlowState != EGameFlowState::Fighting)
    {
        PlayerBarOpacity = 0.0f;
        bPlayerBarsFadingIn = false;
        BossBarOpacity = 0.0f;
        bBossBarFadingIn = false;
    }

    // Draw based on current state
    switch (FlowState)
    {
    case EGameFlowState::StartMenu:
        DrawStartMenu(ScreenW, ScreenH);
        break;

    case EGameFlowState::Fighting:
        DrawPlayerBars(ScreenW, ScreenH, DeltaTime);
        DrawBossHealthBar(ScreenW, ScreenH, DeltaTime);
        break;

    case EGameFlowState::Defeat:
        DrawDeathScreen(ScreenW, ScreenH, L"YOU DIED", false);
        break;

    case EGameFlowState::Victory:
        DrawDeathScreen(ScreenW, ScreenH, L"DEMIGOD FELLED", true);
        break;

    default:
        break;
    }

    // 디버그 상태는 항상 표시 (FPS처럼)
    DrawDebugStats(ScreenW, ScreenH);

    // Pop clip and reset transform
    D2DContext->PopAxisAlignedClip();
    D2DContext->SetTransform(D2D1::Matrix3x2F::Identity());

    D2DContext->EndDraw();
    D2DContext->SetTarget(nullptr);

    SafeRelease(TargetBmp);
    SafeRelease(Surface);
}
