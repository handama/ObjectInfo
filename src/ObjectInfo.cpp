#include <YRPP.h>
#include <Helpers/Macro.h>
#include <TagTypeClass.h>
#include <TriggerTypeClass.h>
#include "command.h"
#include "GeneralUtils.h"
#include "Rules.h"
#include "ToolTipManager.h"
#include <map>
#include <WWMouseClass.h>
#include <cmath> 
#include <string>
#include <cwchar>
#include <cwctype>
#include <algorithm>
#include <variant>

#define COLOR_ALLIEDTT 0xA69F
#define COLOR_SOVIETTT 0xFFE0

static wchar_t* char2wchar(const char* cchar)
{
	wchar_t* m_wchar;
	int len = MultiByteToWideChar(CP_ACP, 0, cchar, strlen(cchar), NULL, 0);
	m_wchar = new wchar_t[len + 1];
	MultiByteToWideChar(CP_ACP, 0, cchar, strlen(cchar), m_wchar, len);
	m_wchar[len] = '\0';
	return m_wchar;
}

int RGB8882RGB565(int r, int g, int b)
{
	unsigned short rgb;
	unsigned char r2 = (unsigned char)r;
	unsigned char g2 = (unsigned char)g;
	unsigned char b2 = (unsigned char)b;

	r2 = r2 >> 3;
	g2 = g2 >> 2;
	b2 = b2 >> 3;

	rgb = ((r & 0b11111000) << 8) | ((g & 0b11111100) << 3) | (b >> 3);
	return (int)rgb;
}

DEFINE_HOOK(4F4583, GScreenClass_DrawOnTop_TheDarkSideOfTheMoon, 6)
{
	for (auto pTechno : *TechnoClass::Array)
	{
		if (pTechno->unknown_bool_431 || pTechno->IsSelected)
		{
			Point2D position;
			TacticalClass::Instance->CoordsToClient(&pTechno->GetCoords(), &position);

			int offsetX = position.X + ObjectInfoDisplay::DisplayOffsetX;
			int offsetY = position.Y + ObjectInfoDisplay::DisplayOffsetY;
			bool opposite = false;
			bool draw = true;

			if (DSurface::Composite->GetHeight() - position.Y < 150)
			{
				opposite = true;
				offsetY = position.Y - 30;
			}
			if ((position.X < -80 || position.X > DSurface::Composite->GetWidth() + 80)
				|| (position.Y < -80 || position.Y > DSurface::Composite->GetHeight() + 80))
				draw = false;

			char displayBuffer[0x100] = { 0 };
			auto DrawToolTipText = [&opposite, &draw](const wchar_t* string, int& offsetX, int& offsetY, int color) {
				if (draw)
				{
					wchar_t string2[0x800];
					swprintf_s(string2, L"%s", string);
					wchar_t string3[0x800];
					swprintf_s(string3, L"%s", string);
					auto h = DSurface::Composite->GetHeight();
					auto w = DSurface::Composite->GetWidth();
					RectangleStruct wanted = { 0, 0, 0, 0 };


					const wchar_t* d = L"\n";
					wchar_t* p;
					p = wcstok(string3, d);
					while (p)
					{
						auto wanted2 = Drawing::GetTextDimensions(p);
						wanted.Height += wanted2.Height + 2;
						if (wanted.Width < wanted2.Width)
							wanted.Width = wanted2.Width;
						p = wcstok(NULL, d);
					}

					int exceedX = w - offsetX - wanted.Width;
					if (exceedX >= 0)
						exceedX = 0;

					int exceedY = h - offsetY - wanted.Height;
					if (exceedY >= 0)
						exceedY = 0;

					RectangleStruct rect = { offsetX + exceedX + 4, offsetY , wanted.Width, wanted.Height };
					RectangleStruct rectBack = { rect.X - 3, rect.Y - 1, rect.Width + 6, rect.Height + 2 };
					RectangleStruct rectLine = { rectBack.X - 1, rectBack.Y - 1, rectBack.Width + 2, rectBack.Height + 2 };

					DSurface::Composite->FillRect(&rectLine, color);
					DSurface::Composite->FillRect(&rectBack, COLOR_BLACK);

					p = wcstok(string2, d);
					while (p)
					{
						auto wanted2 = Drawing::GetTextDimensions(p);
						DSurface::Composite->DrawTextA(p, rect.X, rect.Y, color);
						rect.Y += wanted2.Height + 2;
						p = wcstok(NULL, d);
					}

					wanted.Height += 2;

					if (opposite)
						offsetY -= wanted.Height;
					else
						offsetY += wanted.Height;
				}
				};

			auto displayToopTip = [&DrawToolTipText, &offsetX, &offsetY, &pTechno](const wchar_t* pFormat, ...)
				{
					wchar_t buffer[0x100] = { 0 };
					va_list args;
					va_start(args, pFormat);
					vswprintf_s(buffer, 0x100, pFormat, args);
					va_end(args);

					auto color = pTechno->Owner->Color;
					DrawToolTipText(buffer, offsetX, offsetY, RGB8882RGB565(color.R, color.G, color.B));

				};


			auto printFoots = [&displayToopTip](FootClass* pFoot)
				{
					auto pType = pFoot->GetTechnoType();
					char missing[0x100] = { 0 };
					sprintf(missing, "MISSING:'%s'", pType->UINameLabel);
					displayToopTip(L"%s", GeneralUtils::LoadStringUnlessMissing(pType->UINameLabel, char2wchar(missing)));

				};
			auto printBuilding = [&displayToopTip](BuildingClass* pBuilding)
				{
					auto pType = pBuilding->GetTechnoType();
					char missing[0x100] = { 0 };
					sprintf(missing, "MISSING:'%s'", pType->UINameLabel);
					displayToopTip(L"%s", GeneralUtils::LoadStringUnlessMissing(pType->UINameLabel, char2wchar(missing)));
				};


			switch (pTechno->WhatAmI())
			{
			case AbstractType::Infantry:
			case AbstractType::Unit:
			case AbstractType::Aircraft:
				printFoots(static_cast<FootClass*>(pTechno));
				break;
			case AbstractType::Building:
				printBuilding(static_cast<BuildingClass*>(pTechno));
				break;
			default:
				break;
			}
		}
	}
	return 0;
}

