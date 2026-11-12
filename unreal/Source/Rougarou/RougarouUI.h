#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
class ARougarouController;
class SBox;

// A small game UI, matching the web app's problems/settings/help hierarchy.
class SRougarouUI : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SRougarouUI){} SLATE_ARGUMENT(ARougarouController*,Controller) SLATE_END_ARGS()
 void Construct(const FArguments& Args);
 void Refresh(){Rebuild();}
 void SelectTab(int32 Index){Tab=Index;Rebuild();}
 virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool) const override;
 virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&) override;
 virtual FReply OnMouseMove(const FGeometry&,const FPointerEvent&) override;
 virtual FReply OnMouseWheel(const FGeometry&,const FPointerEvent&) override;
 virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&) override;
private:
 TSharedPtr<SBox> FormBody;
 TArray<TSharedPtr<FString>> FormBones,FormClips;
 int32 DragAxis=-1, DragJoint=INDEX_NONE;
 FVector AnchorStart, AnchorPlane, AnchorNormal;
 FTransform AnchorParent;
 FVector2f DragStart,DragDirection,DragCenter,DragU,DragV;
 float DragAngle=0;
 bool RingDrag=false,OrbitDrag=false;
 float DragSign=1;
 float DragValue=0;
 bool FormAxes(const FGeometry&,FVector2f&,TArray<FVector2f>&) const;
 void RebuildForm();
 TWeakObjectPtr<ARougarouController> Owner;
 TSharedPtr<SBox> Body;
 int32 Tab=0;
 FButtonStyle PlainStyle,AccentStyle;
 FSlateRoundedBoxBrush Panel{FLinearColor(.025,.03,.035,.97),18.f};
 FSlateRoundedBoxBrush Card{FLinearColor(.055,.065,.073,1),10.f};
 FSlateRoundedBoxBrush Scrim{FLinearColor(0,0,0,.38),0.f};
 void Rebuild();
 TSharedRef<SWidget> Text(FString Value,int32 Size=14,FLinearColor Color=FLinearColor(.87,.86,.81)) const;
 TSharedRef<SWidget> Button(FString Label,TFunction<void()> Action,bool Primary=false) const;
};
