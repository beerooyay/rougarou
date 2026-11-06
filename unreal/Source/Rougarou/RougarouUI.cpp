#include "RougarouUI.h"
#include "RougarouGame.h"
#include "ClimberActor.h"
#include "GymActor.h"
#include "Camera/CameraActor.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Rendering/DrawElements.h"

static const FLinearColor Ivory(.9,.88,.82),Muted(.47,.52,.54),Gold(.84,.67,.36);
TSharedRef<SWidget> SRougarouUI::Text(FString Value,int32 Size,FLinearColor Color) const
{
 return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle(Size>=24?"Bold":"Regular",Size)).ColorAndOpacity(Color).AutoWrapText(true);
}
TSharedRef<SWidget> SRougarouUI::Button(FString Label,TFunction<void()> Action,bool Primary) const
{
 return SNew(SButton).IsFocusable(false).ContentPadding(FMargin(15,10))
  .ButtonStyle(Primary?&AccentStyle:&PlainStyle)
  .ButtonColorAndOpacity(FLinearColor::White)
  .OnClicked_Lambda([Action](){Action();return FReply::Handled();})
  [Text(Label,13,Primary?FLinearColor(.025,.03,.035):Ivory)];
}
void SRougarouUI::Construct(const FArguments& Args)
{
 Owner=Args._Controller;
 PlainStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.07,.08,.09),6.f)).SetHovered(FSlateRoundedBoxBrush(FLinearColor(.12,.14,.15),6.f)).SetPressed(FSlateRoundedBoxBrush(FLinearColor(.045,.05,.06),6.f));
 AccentStyle.SetNormal(FSlateRoundedBoxBrush(Gold,6.f)).SetHovered(FSlateRoundedBoxBrush(FLinearColor(.95,.8,.48),6.f)).SetPressed(FSlateRoundedBoxBrush(FLinearColor(.65,.49,.23),6.f));
 FParse::Value(FCommandLine::Get(),TEXT("uitab="),Tab);
 auto VisibleHUD=[this](){return Owner.IsValid()&&!Owner->MenuOpen?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed;};
 TSharedPtr<SHorizontalBox> Actions;
 ChildSlot
 [SNew(SOverlay)
  +SOverlay::Slot().Padding(26).HAlign(HAlign_Left).VAlign(VAlign_Top)
  [SNew(SBorder).BorderImage(&Panel).Padding(FMargin(20,15)).Visibility_Lambda(VisibleHUD)
   [SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[Text(TEXT("ROUGAROU / THE ROCK LOUNGE"),10,Gold)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,7,0,3)
     [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(Owner.IsValid()?Owner->Title():TEXT(""));}).Font(FCoreStyle::GetDefaultFontStyle("Bold",21)).ColorAndOpacity(Ivory)]
    +SVerticalBox::Slot().AutoHeight()
     [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(Owner.IsValid()?Owner->Hint():TEXT(""));}).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).ColorAndOpacity(Muted)]
   ]
  ]
  +SOverlay::Slot().Padding(26).HAlign(HAlign_Right).VAlign(VAlign_Top)
   [SNew(SHorizontalBox).Visibility_Lambda(VisibleHUD)
    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[Button(TEXT("change view  /  v"),[this](){if(Owner.IsValid())Owner->TogglePerspective();})]
    +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("menu  /  m"),[this](){if(Owner.IsValid()){Owner->ToggleMenu();Rebuild();}})]
   ]
  +SOverlay::Slot().Padding(26).HAlign(HAlign_Center).VAlign(VAlign_Bottom)
   [SNew(SVerticalBox).Visibility_Lambda(VisibleHUD)
    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,12)
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().AutoWidth().Padding(4,0)
      [SNew(SBox).Visibility_Lambda([this](){return Owner.IsValid()&&Owner->IsClimbing()?EVisibility::Visible:EVisibility::Collapsed;})
       [Button(TEXT("retry"),[this](){Owner->Retry();})]]
     +SHorizontalBox::Slot().AutoWidth().Padding(4,0)
      [SNew(SBox).Visibility_Lambda([this](){return Owner.IsValid()&&Owner->IsClimbing()?EVisibility::Visible:EVisibility::Collapsed;})
       [Button(TEXT("jump down"),[this](){Owner->DropToFloor();})]]
     +SHorizontalBox::Slot().AutoWidth().Padding(4,0)
      [SNew(SBox).Visibility_Lambda([this](){return Owner.IsValid()&&!Owner->IsClimbing()&&Owner->NearestProblem()>=0?EVisibility::Visible:EVisibility::Collapsed;})
       [Button(TEXT("pull on  /  e"),[this](){Owner->PullOn();},true)]]
     +SHorizontalBox::Slot().AutoWidth().Padding(4,0)
      [SNew(SBox).Visibility_Lambda([this](){return Owner.IsValid()&&Owner->FirstPerson&&Owner->IsClimbing()?EVisibility::Visible:EVisibility::Collapsed;})
       [SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().Padding(3,0)[Button(TEXT("right hand"),[this](){Owner->SendLimb(2);})]
        +SHorizontalBox::Slot().AutoWidth().Padding(3,0)[Button(TEXT("left hand"),[this](){Owner->SendLimb(3);})]
        +SHorizontalBox::Slot().AutoWidth().Padding(3,0)[Button(TEXT("right foot"),[this](){Owner->SendLimb(0);})]
        +SHorizontalBox::Slot().AutoWidth().Padding(3,0)[Button(TEXT("left foot"),[this](){Owner->SendLimb(1);})]
       ]]
    ]
    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
    [SNew(SBorder).BorderImage(&Card).Padding(FMargin(18,9))
     [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(Owner.IsValid()?Owner->Keys():TEXT(""));}).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).ColorAndOpacity(Muted)]]
   ]
  +SOverlay::Slot().Padding(26,150,26,26).HAlign(HAlign_Left).VAlign(VAlign_Top)
   [SNew(SBox).WidthOverride(195).Visibility_Lambda([this](){return Owner.IsValid()&&!Owner->MenuOpen&&Owner->IsClimbing()&&Owner->ShowGauges?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed;})
    [SNew(SBorder).BorderImage(&Panel).Padding(14)
     [SNew(SVerticalBox)
      +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Text(TEXT("FOREARMS"),10,Muted)]
      +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,7)
       [SNew(SProgressBar).Percent_Lambda([this](){return Owner.IsValid()?Owner->Climber->Sim.Pump[3]:0.f;}).FillColorAndOpacity(Gold)]
      +SVerticalBox::Slot().AutoHeight()
       [SNew(SProgressBar).Percent_Lambda([this](){return Owner.IsValid()?Owner->Climber->Sim.Pump[2]:0.f;}).FillColorAndOpacity(Gold)]
     ]]
   ]
  +SOverlay::Slot()
   [SNew(SBorder).BorderImage(&Scrim).Padding(30).Visibility_Lambda([this](){return Owner.IsValid()&&Owner->MenuOpen?EVisibility::Visible:EVisibility::Collapsed;})
    [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
     [SNew(SBox).WidthOverride(790).HeightOverride(650)
      [SNew(SBorder).BorderImage(&Panel).Padding(30)
       [SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()
         [SNew(SHorizontalBox)
          +SHorizontalBox::Slot().FillWidth(1)
           [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[Text(TEXT("THE ROCK LOUNGE"),10,Gold)]
            +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,22)[Text(TEXT("rougarou"),38,Ivory)]]
          +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
           [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(FString::Printf(TEXT("%d / 9 sent"),Owner.IsValid()?Owner->Sends.Num():0));}).Font(FCoreStyle::GetDefaultFontStyle("Regular",13)).ColorAndOpacity(Muted)]
         ]
        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,20)
         [SNew(SHorizontalBox)
          +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[Button(TEXT("problems"),[this](){Tab=0;Rebuild();})]
          +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[Button(TEXT("settings"),[this](){Tab=1;Rebuild();})]
          +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("how to climb"),[this](){Tab=2;Rebuild();})]
         ]
        +SVerticalBox::Slot().FillHeight(1)[SAssignNew(Body,SBox)]
        +SVerticalBox::Slot().AutoHeight().Padding(0,20,0,0)
         [SNew(SHorizontalBox)
          +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(TEXT("find a line. take your time."),12,Muted)]
          +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("back to the gym"),[this](){if(Owner->ReferencePreview)Owner->ToggleReferencePreview();else Owner->ToggleMenu();},true)]
         ]
       ]
      ]
     ]
    ]
   ]
 ];
 Rebuild();
}
void SRougarouUI::Rebuild()
{
 if(!Body.IsValid()||!Owner.IsValid())return;
 if(Tab==0)
 {
  TSharedRef<SUniformGridPanel> Grid=SNew(SUniformGridPanel).SlotPadding(FMargin(5));
  const auto Routes=AGymActor::Problems();
  const TCHAR* Grades[]={TEXT("V2"),TEXT("V4"),TEXT("V3"),TEXT("V5"),TEXT("V3"),TEXT("V2"),TEXT("V1"),TEXT("V1"),TEXT("V2")};
  for(int32 I=0;I<Routes.Num();I++)
  {
   Grid->AddSlot(I%2,I/2)
   [SNew(SBorder).BorderImage(&Card).Padding(16)
    [SNew(SVerticalBox)
     +SVerticalBox::Slot().AutoHeight()
      [SNew(SHorizontalBox)
       +SHorizontalBox::Slot().FillWidth(1)[Text(Routes[I].Name,18,Ivory)]
       +SHorizontalBox::Slot().AutoWidth()[Text(Grades[I],12,AGymActor::Tape(I))]]
     +SVerticalBox::Slot().AutoHeight().Padding(0,5,0,12)[Text(Owner->Sends.Contains(Routes[I].Name)?TEXT("sent"):TEXT("a fresh line"),11,Muted)]
     +SVerticalBox::Slot().AutoHeight()
      [SNew(SHorizontalBox)
       +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[Button(TEXT("climb it"),[this,I](){Owner->VisitProblem(I,true);},true)]
       +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("walk there"),[this,I](){Owner->VisitProblem(I,false);})]]
    ]
   ];
  }
  Body->SetContent(SNew(SScrollBox)+SScrollBox::Slot()[Grid]);
 }
 else if(Tab==1)
 {
  TSharedRef<SVerticalBox> Settings=SNew(SVerticalBox);
  auto Slider=[&](FString Label,float Min,float Max,float* Value)
  {
   Settings->AddSlot().AutoHeight().Padding(6,9)[Text(Label,13,Ivory)];
   Settings->AddSlot().AutoHeight().Padding(6,0,6,16)
    [SNew(SSlider).Value_Lambda([Value,Min,Max](){return (*Value-Min)/(Max-Min);})
     .OnValueChanged_Lambda([this,Value,Min,Max](float V){*Value=FMath::Lerp(Min,Max,V);Owner->SaveSettings();})];
  };
  Slider(TEXT("look sensitivity"),.3,2.5,&Owner->LookSensitivity);
  Slider(TEXT("third-person distance"),.5,1.8,&Owner->CameraDistance);
  auto Toggle=[&](FString Label,bool* Value,TFunction<void()> After)
  {
   Settings->AddSlot().AutoHeight().Padding(6,9)
    [SNew(SCheckBox).IsChecked_Lambda([Value](){return *Value?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
     .OnCheckStateChanged_Lambda([this,Value,After](ECheckBoxState State){*Value=State==ECheckBoxState::Checked;After();Owner->SaveSettings();})
     [SNew(SBox).Padding(FMargin(10,0))[Text(Label,14,Ivory)]]];
  };
  Toggle(TEXT("trackpad look (two-finger swipe / Option + move)"),&Owner->TrackpadLook,[](){});
  Toggle(TEXT("clothes movement"),&Owner->FabricMotion,[](){});
  Toggle(TEXT("forearm gauges"),&Owner->ShowGauges,[](){});
  Toggle(TEXT("high quality rendering"),&Owner->HighQuality,[this](){Owner->ApplyQuality();});
  Settings->AddSlot().AutoHeight().Padding(6,22,6,8)[Text(TEXT("Two-finger swipe looks around; menus scroll normally. V switches between first and third person. Settings and your tick list save on this Mac."),12,Muted)];
  Settings->AddSlot().AutoHeight().Padding(6,8)[Button(Owner->ReferencePreview?TEXT("leave motion preview"):TEXT("preview release / regrip"),[this](){Owner->ToggleReferencePreview();})];
  Settings->AddSlot().AutoHeight().Padding(6,8)[Text(TEXT("Recorded assisted reference, not a learned policy. 2/4 practice grips; 0/4 wider placements. Two-finger swipe or Option + move orbits; J returns to the gym."),12,Muted)];
  Body->SetContent(SNew(SScrollBox)+SScrollBox::Slot()[Settings]);
 }
 else
 {
  TSharedRef<SVerticalBox> Help=SNew(SVerticalBox);
  const TPair<FString,FString> Sections[]={
   {TEXT("walk the gym"),TEXT("WASD moves. Shift runs. Two-finger swipe or hold Option and move the pointer to look around. Right-drag also works while walking. V switches views. Walk up to a problem and press E, or choose one from the problems menu.")},
   {TEXT("climb with your whole body"),TEXT("Click a hand or foot, reach, then click to place it. WASD shifts your hips. X releases, J jumps down, R retries. Match both hands on the top hold to send.")},
   {TEXT("through your own eyes"),TEXT("V enters first person. Look at a hold: the ring shows your target. Press R or L for the side, then A for the arm or L for the leg. 1/2 also reach with your hands; 3/4 move your feet.")},
   {TEXT("find your balance"),TEXT("In first person, arrow keys latch a hip shift. Press the same arrow again to release it; Space centers you. Retry lives on the button, since R chooses a side. M opens the menu.")}
  };
  for(const auto& Section:Sections)
  {
   Help->AddSlot().AutoHeight().Padding(6,0,6,6)[Text(Section.Key,19,Ivory)];
   Help->AddSlot().AutoHeight().Padding(6,0,6,20)[Text(Section.Value,13,Muted)];
  }
  Body->SetContent(SNew(SScrollBox)+SScrollBox::Slot()[Help]);
 }
}
int32 SRougarouUI::OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,FSlateWindowElementList& Draw,int32 Layer,const FWidgetStyle& Style,bool Enabled) const
{
 Layer=SCompoundWidget::OnPaint(Args,Geometry,Cull,Draw,Layer,Style,Enabled);
 if(!Owner.IsValid()||Owner->MenuOpen||Owner->FirstBlend<.9)return Layer;
 const FVector2f Center=Geometry.GetLocalSize()*.5f;
 auto Ring=[&](FVector2f At,float Radius,FLinearColor Color,float Width)
 {
  TArray<FVector2f> Points;
  for(int32 I=0;I<=40;I++){const float A=2*PI*I/40;Points.Add(At+FVector2f(FMath::Cos(A),FMath::Sin(A))*Radius);}
  FSlateDrawElement::MakeLines(Draw,++Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width);
 };
 Ring(Center,Owner->AimedHold>=0?3:2,Owner->AimedHold>=0?Gold:Ivory,2);
 if(Owner->AimedHold>=0 && Owner->Climber && Owner->View)
 {
  const FClimbRoute& Route=Owner->Climber->Sim.Spec.Routes[Owner->Climber->Sim.Route];
  FVector2D Screen;
  if(Owner->ProjectWorldLocationToScreen(Route.Map(Route.Holds[Owner->AimedHold].Point,8),Screen,true))
  {
   const float Distance=FVector::Distance(Owner->View->GetActorLocation(),Route.Map(Route.Holds[Owner->AimedHold].Point,8));
   // Viewport projection is in pixels; Slate may be scaled for Retina.
   int32 W,H;Owner->GetViewportSize(W,H);
   const FVector2f At(Screen.X*Geometry.GetLocalSize().X/FMath::Max(W,1),Screen.Y*Geometry.GetLocalSize().Y/FMath::Max(H,1));
   Ring(At,FMath::Clamp(1100.f/FMath::Max(Distance,20.f),9.f,30.f),Gold.CopyWithNewOpacity(.7f),2);
  }
 }
 return Layer;
}
