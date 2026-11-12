#include "RougarouUI.h"
#include "RougarouGame.h"
#include "ClimberActor.h"
#include "GymActor.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Rendering/DrawElements.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "LocomotionAnim.h"
#include "Animation/AnimSequence.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"

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
  [SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).ColorAndOpacity(Primary?FLinearColor(.025,.03,.035):Ivory)];
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
    +SVerticalBox::Slot().AutoHeight()[Text(TEXT("THE ROCK LOUNGE"),11,Gold)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,7,0,3)
     [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(Owner.IsValid()?Owner->Title():TEXT(""));}).Font(FCoreStyle::GetDefaultFontStyle("Bold",21)).ColorAndOpacity(Ivory)]
    +SVerticalBox::Slot().AutoHeight()
     [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(Owner.IsValid()?Owner->Hint():TEXT(""));}).Font(FCoreStyle::GetDefaultFontStyle("Regular",12)).ColorAndOpacity(Muted)]
   ]
  ]
  +SOverlay::Slot().Padding(26).HAlign(HAlign_Right).VAlign(VAlign_Top)
   [SNew(SHorizontalBox).Visibility_Lambda(VisibleHUD)
    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[Button(TEXT("View · V"),[this](){if(Owner.IsValid())Owner->TogglePerspective();})]
    +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Menu · M"),[this](){if(Owner.IsValid()){Owner->ToggleMenu();Rebuild();}})]
   ]
  +SOverlay::Slot().Padding(26).HAlign(HAlign_Center).VAlign(VAlign_Bottom)
   [SNew(SVerticalBox).Visibility_Lambda(VisibleHUD)
    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,12)
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().AutoWidth().Padding(4,0)
      [SNew(SBox).Visibility_Lambda([this](){return Owner.IsValid()&&Owner->IsClimbing()?EVisibility::Visible:EVisibility::Collapsed;})
       [Button(TEXT("Retry · R"),[this](){Owner->Retry();})]]
     +SHorizontalBox::Slot().AutoWidth().Padding(4,0)
      [SNew(SBox).Visibility_Lambda([this](){return Owner.IsValid()&&Owner->IsClimbing()?EVisibility::Visible:EVisibility::Collapsed;})
       [Button(TEXT("Drop · J"),[this](){Owner->DropToFloor();})]]
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
      +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Text(TEXT("FOREARM FATIGUE"),11,Muted)]
      +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,7)
       [SNew(SProgressBar).Percent_Lambda([this](){return Owner.IsValid()?Owner->Climber->Sim.Pump[3]:0.f;}).FillColorAndOpacity(Gold)]
      +SVerticalBox::Slot().AutoHeight()
       [SNew(SProgressBar).Percent_Lambda([this](){return Owner.IsValid()?Owner->Climber->Sim.Pump[2]:0.f;}).FillColorAndOpacity(Gold)]
     ]]
   ]
  +SOverlay::Slot()
   [SNew(SBorder).BorderImage(&Scrim).Padding(30).Visibility_Lambda([this](){return Owner.IsValid()&&Owner->MenuOpen&&(!Owner->Climber||!Owner->Climber->Form.Open)?EVisibility::Visible:EVisibility::Collapsed;})
    [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
     [SNew(SBox).WidthOverride(980).HeightOverride(650)
      [SNew(SBorder).BorderImage(&Panel).Padding(24)
       [SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,24,0)
        [SNew(SBox).WidthOverride(204)
         [SNew(SBorder).BorderImage(&Card).Padding(18)
          [SNew(SVerticalBox)
           +SVerticalBox::Slot().AutoHeight()[Text(TEXT("THE ROCK LOUNGE"),11,Gold)]
           +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,28)[Text(TEXT("rougarou"),31,Ivory)]
           +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Button(TEXT("Problems"),[this](){SelectTab(0);})]
           +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Button(TEXT("Settings"),[this](){SelectTab(1);})]
           +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Button(TEXT("Controls"),[this](){SelectTab(2);})]
           +SVerticalBox::Slot().AutoHeight()[Button(TEXT("Player form"),[this](){Owner->TogglePlayerForm();})]
           +SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
           +SVerticalBox::Slot().AutoHeight()[Button(TEXT("Back to gym · M"),[this](){if(Owner->ReferencePreview)Owner->ToggleReferencePreview();else Owner->ToggleMenu();},true)]
          ]
         ]
        ]
        +SHorizontalBox::Slot().FillWidth(1)
        [SNew(SVerticalBox)
         +SVerticalBox::Slot().AutoHeight().Padding(2,2,10,17)
         [SNew(SHorizontalBox)
          +SHorizontalBox::Slot().FillWidth(1)
          [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",28)).ColorAndOpacity(Ivory)
           .Text_Lambda([this](){return FText::FromString(Tab==0?TEXT("Problems"):Tab==1?TEXT("Settings"):TEXT("Controls"));})]
          +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
          [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).ColorAndOpacity(Gold)
           .Visibility_Lambda([this](){return Tab==0?EVisibility::Visible:EVisibility::Collapsed;})
           .Text_Lambda([this](){return FText::FromString(FString::Printf(TEXT("%d / %d sent"),Owner.IsValid()?Owner->Sends.Num():0,Owner.IsValid()&&Owner->Climber?Owner->Climber->Sim.Spec.Routes.Num():9));})]
         ]
         +SVerticalBox::Slot().FillHeight(1)[SAssignNew(Body,SBox)]
        ]
       ]
      ]
     ]
    ]
   ]
  +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Fill).Padding(18)
   [SNew(SBox).WidthOverride(365).Visibility_Lambda([this](){return Owner.IsValid()&&Owner->Climber&&Owner->Climber->Form.Open?EVisibility::Visible:EVisibility::Collapsed;})
    [SNew(SBorder).BorderImage(&Panel).Padding(18)[SAssignNew(FormBody,SBox)]]]
 ];
 Rebuild();
}
void SRougarouUI::Rebuild()
{
 if(!Body.IsValid()||!Owner.IsValid())return;
 if(Owner->Climber && Owner->Climber->Form.Open){RebuildForm();return;}
 if(Tab==0)
 {
  TSharedRef<SScrollBox> List=SNew(SScrollBox);
  const auto Routes=AGymActor::Problems();
  const TCHAR* Grades[]={TEXT("V2"),TEXT("V4"),TEXT("V3"),TEXT("V5"),TEXT("V3"),TEXT("V2"),TEXT("V1"),TEXT("V1"),TEXT("V2")};
  const TCHAR* Styles[]={TEXT("Slab · balance and small edges"),TEXT("Overhang · tension and direction changes"),TEXT("Kicker · feet through the angle change"),
   TEXT("Roof · keep the feet engaged"),TEXT("Changing angles · shift your weight"),TEXT("Overhang · precise pockets"),TEXT("Steep face · pinches and long pulls"),
   TEXT("Arch · outside edge"),TEXT("Cave · sustained overhang")};
  for(int32 I=0;I<Routes.Num();I++)
  {
   const bool Sent=Owner->Sends.Contains(Routes[I].Name);
   List->AddSlot().Padding(0,0,10,7)
   [SNew(SBorder).BorderImage(&Card).Padding(FMargin(14,10))
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,14,0)
     [SNew(SBox).WidthOverride(32)[Text(I<UE_ARRAY_COUNT(Grades)?Grades[I]:TEXT("—"),15,AGymActor::Tape(I))]]
     +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
     [SNew(SVerticalBox)
      +SVerticalBox::Slot().AutoHeight()[Text(Routes[I].Name,17,Ivory)]
      +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[Text(I<UE_ARRAY_COUNT(Styles)?Styles[I]:TEXT(""),12,Muted)]]
     +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)
     [SNew(SBox).WidthOverride(36)[Text(Sent?TEXT("Sent"):TEXT(""),11,Gold)]]
     +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)[Button(TEXT("Visit"),[this,I](){Owner->VisitProblem(I,false);})]
     +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(TEXT("Climb"),[this,I](){Owner->VisitProblem(I,true);},true)]
    ]
   ];
  }
  Body->SetContent(List);
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
  Toggle(TEXT("climbing trackpad look (two-finger swipe / Option + move)"),&Owner->TrackpadLook,[](){});
  Toggle(TEXT("invert trackpad look"),&Owner->InvertLook,[](){});
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
   {TEXT("walk the gym"),TEXT("WASD moves. Hold either Shift key to sprint. Move the pointer to look around. Escape frees the pointer and clicking recaptures it. V switches views. Walk up to a problem and press E, or choose one from the problems menu.")},
   {TEXT("climb with your whole body"),TEXT("Q and E are your left and right hands, Z and C your left and right feet. Press one (or click the limb) to pick it up, reach with the pointer, and click to place it. Press the same key again to put it back; X lets it hang. WASD shifts your hips. Match both hands on the top hold to send.")},
   {TEXT("through your own eyes"),TEXT("V enters first person. Look at a hold until the ring finds it, then press Q or E to take it with a hand, Z or C to step on it. WASD still shifts your hips. Escape frees the pointer for the buttons.")},
   {TEXT("every time"),TEXT("R retries the problem, J jumps down to the mats, G shows the whole gym, and the bracket keys step through the problems. M opens this menu.")}
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
 if(Owner.IsValid() && Owner->Climber && Owner->Climber->Form.Open)
 {
  auto* Mesh=Owner->Climber->Walker.Get();auto& Form=Owner->Climber->Form;
  int32 W=1,H=1;Owner->GetViewportSize(W,H);
  auto ProjectJoint=[&](int32 I,FVector2f& P){FVector2D V;if(!Owner->ProjectWorldLocationToScreen(Mesh->GetBoneLocation(Mesh->GetBoneName(I)),V,true))return false;P=FVector2f(V.X*Geometry.GetLocalSize().X/FMath::Max(W,1),V.Y*Geometry.GetLocalSize().Y/FMath::Max(H,1));return true;};
  const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
  for(const auto& J:Form.Draft.JointLimits())
  {
   FVector2f P;if(!ProjectJoint(J.Key,P))continue;
   int32 Parent=Ref.GetParentIndex(J.Key);while(Parent>=0&&!Form.Draft.JointLimits().Contains(Parent))Parent=Ref.GetParentIndex(Parent);
   FVector2f Q;if(Parent>=0&&ProjectJoint(Parent,Q)){TArray<FVector2f> Link{P,Q};FSlateDrawElement::MakeLines(Draw,++Layer,Geometry.ToPaintGeometry(),Link,ESlateDrawEffect::None,FLinearColor(.55,.72,.85,.6),true,1.5f);}
   TArray<FVector2f> Dot;const float Radius=J.Key==Form.Selected?8:5;
   for(int32 I=0;I<=20;++I){const float A=2*PI*I/20;Dot.Add(P+FVector2f(FMath::Cos(A),FMath::Sin(A))*Radius);}
   FSlateDrawElement::MakeLines(Draw,++Layer,Geometry.ToPaintGeometry(),Dot,ESlateDrawEffect::None,J.Key==Form.Selected?Gold:FLinearColor(.7,.84,.94),true,3);
  }
  FVector2f Center;TArray<FVector2f> Ends;
  if(FormAxes(Geometry,Center,Ends))
  {
   const FLinearColor Colors[]={FLinearColor(1,.22,.12),FLinearColor(.2,1,.35),FLinearColor(.2,.55,1)};
   for(int32 I=0;I<3;++I)
   {
    const FVector2f U=Ends[(I+1)%3]-Center,V=Ends[(I+2)%3]-Center;
    TArray<FVector2f> Circle;for(int32 J=0;J<=80;++J){const float A=2*PI*J/80;Circle.Add(Center+U*FMath::Cos(A)+V*FMath::Sin(A));}
    FSlateDrawElement::MakeLines(Draw,++Layer,Geometry.ToPaintGeometry(),Circle,ESlateDrawEffect::None,Colors[I].CopyWithNewOpacity(.45),true,1.5f);
    if(Form.ShowLimits)
    {
     TArray<FVector2f> Arc;for(int32 J=0;J<=60;++J){const float A=FMath::DegreesToRadians(FMath::Lerp(Form.Bound(I,false),Form.Bound(I,true),J/60.f));Arc.Add(Center+U*FMath::Cos(A)+V*FMath::Sin(A));}
     FSlateDrawElement::MakeLines(Draw,++Layer,Geometry.ToPaintGeometry(),Arc,ESlateDrawEffect::None,Colors[I],true,4);
    }
   }
   FSlateDrawElement::MakeText(Draw,++Layer,Geometry.ToPaintGeometry(FVector2f(180,25),FSlateLayoutTransform(Center+FVector2f(12,-28))),Mesh->GetBoneName(Form.Selected).ToString(),FCoreStyle::GetDefaultFontStyle("Bold",13),ESlateDrawEffect::None,Gold);
  }
  return Layer;
 }
 if(!Owner.IsValid()||Owner->MenuOpen||!Owner->Climber||!Owner->View)return Layer;
 auto Ring=[&](FVector2f At,float Radius,FLinearColor Color,float Width)
 {
  TArray<FVector2f> Points;
  for(int32 I=0;I<=48;I++){const float A=2*PI*I/48;Points.Add(At+FVector2f(FMath::Cos(A),FMath::Sin(A))*Radius);}
  FSlateDrawElement::MakeLines(Draw,++Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width);
 };
 // viewport projection is in pixels; Slate may be scaled for Retina
 int32 ViewW=1,ViewH=1;Owner->GetViewportSize(ViewW,ViewH);
 auto Project=[&](const FVector& World,FVector2f& Out)
 {
  FVector2D Screen;if(!Owner->ProjectWorldLocationToScreen(World,Screen,true))return false;
  Out=FVector2f(Screen.X*Geometry.GetLocalSize().X/FMath::Max(ViewW,1),Screen.Y*Geometry.GetLocalSize().Y/FMath::Max(ViewH,1));return true;
 };
 const AClimberActor* Who=Owner->Climber;
 if(Owner->IsClimbing() && Owner->FirstBlend<.5f && Who->Selected>=0 && Who->Selected<4)
 {
  // the lifted hand or foot, and the hold it is lined up on: green when the body can take it, red when it cannot
  const FClimbSim& Sim=Who->Sim;const FVector Side=Owner->View->GetActorRightVector();
  FVector2f At,Edge;
  const FVector Tip=Who->Place(Sim.Tips[Who->Selected],Sim.Arrived(Who->Selected)?4:Who->Gap*.5f);
  if(Project(Tip,At)&&Project(Tip+Side*7,Edge))
  {
   const float R=FMath::Max(8.f,(Edge-At).Size());
   Ring(At,R+1.5f,FLinearColor(0,0,0,.35f),4);Ring(At,R,Gold,2);
  }
  if(Sim.Holds().IsValidIndex(Who->Hovered))
  {
   const FClimbHold& H=Sim.Holds()[Who->Hovered];
   const FVector N=H.Normal.IsNearlyZero()?Sim.Spec.Routes[Sim.Route].At(H.Point.Y).Out:H.Normal;
   const FVector C=H.Radius>0?H.Center+N*(H.Height+1):Who->Place(H.Point,4);
   if(Project(C,At)&&Project(C+Side*(FMath::Max(H.Radius,5.f)+4),Edge))
   {
    const float R=FMath::Max(12.f,(Edge-At).Size());
    Ring(At,R+1.5f,FLinearColor(0,0,0,.35f),5);
    Ring(At,R,Who->HoverReady?FLinearColor(.42f,.9f,.58f):FLinearColor(.95f,.4f,.34f),2.5f);
   }
  }
 }
 if(Owner->FirstBlend<.9)return Layer;
 const FVector2f Center=Geometry.GetLocalSize()*.5f;
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

void SRougarouUI::RebuildForm()
{
 if(!FormBody.IsValid()||!Owner.IsValid()||!Owner->Climber)return;
 auto& F=Owner->Climber->Form;auto* Mesh=Owner->Climber->Walker.Get();
 if(!Mesh||!Mesh->GetSkeletalMeshAsset())return;
 FormBones.Reset();for(const auto& Pair:F.Draft.JointLimits())FormBones.Add(MakeShared<FString>(Mesh->GetBoneName(Pair.Key).ToString()));
 FormBones.Sort([](const TSharedPtr<FString>& A,const TSharedPtr<FString>& B){return *A<*B;});
 TSharedPtr<FString> Selected;for(auto Bone:FormBones)if(*Bone==Mesh->GetBoneName(F.Selected).ToString())Selected=Bone;
 FormClips.Reset();for(const TCHAR* Label:{TEXT("neutral rig"),TEXT("idle"),TEXT("walk"),TEXT("jog"),TEXT("run"),TEXT("start"),TEXT("stop"),TEXT("fall / landing")})FormClips.Add(MakeShared<FString>(Label));
 TSharedRef<SVerticalBox> Rows=SNew(SVerticalBox);auto Add=[&](TSharedRef<SWidget> W){Rows->AddSlot().AutoHeight().Padding(0,4)[W];};
 Add(SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&FormBones).InitiallySelectedItem(Selected)
  .OnGenerateWidget_Lambda([this](TSharedPtr<FString> S){return Text(*S,12);})
  .OnSelectionChanged_Lambda([this](TSharedPtr<FString> S,ESelectInfo::Type){if(S.IsValid()){Owner->Climber->Form.Selected=Owner->Climber->Walker->GetBoneIndex(**S);RebuildForm();}})
  [SNew(STextBlock).Text_Lambda([this](){return FText::FromName(Owner->Climber->Walker->GetBoneName(Owner->Climber->Form.Selected));})]);
 Add(SNew(SCheckBox).IsChecked_Lambda([this](){return Owner->Climber->Form.Mirror?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
  .OnCheckStateChanged_Lambda([this](ECheckBoxState V){Owner->Climber->Form.Mirror=V==ECheckBoxState::Checked;})[Text(TEXT("mirror left / right"),12)]);
 const FLinearColor Colors[]={FLinearColor(1,.3,.2),FLinearColor(.3,.85,.45),FLinearColor(.3,.6,1)};
 for(int32 Axis=0;Axis<3;++Axis)
 {
  Add(Text(Axis==0?TEXT("bend"):Axis==1?TEXT("side"):TEXT("twist"),15,Colors[Axis]));
  Add(SNew(SSpinBox<float>).MinValue(-180).MaxValue(180).Delta(1)
   .Value_Lambda([this,Axis](){return Owner->Climber->Form.Get(Axis);})
   .OnValueChanged_Lambda([this,Axis](float V){Owner->Climber->Form.Set(*Owner->Climber->Walker,Axis,V);}));
  Add(SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth().Padding(0,4,8,0)[Text(TEXT("min"),11,Muted)]
   +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(-180).MaxValue(Axis<2?0:180).Delta(1)
    .Value_Lambda([this,Axis](){return Owner->Climber->Form.Bound(Axis,false);})
    .OnValueChanged_Lambda([this,Axis](float V){Owner->Climber->Form.SetBound(*Owner->Climber->Walker,Axis,false,V);})]
   +SHorizontalBox::Slot().AutoWidth().Padding(8,4,8,0)[Text(TEXT("max"),11,Muted)]
   +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(Axis<2?0:-180).MaxValue(180).Delta(1)
    .Value_Lambda([this,Axis](){return Owner->Climber->Form.Bound(Axis,true);})
    .OnValueChanged_Lambda([this,Axis](float V){Owner->Climber->Form.SetBound(*Owner->Climber->Walker,Axis,true,V);})]);
  Add(SNew(SHorizontalBox)
   +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("set min"),[this,Axis](){auto& P=Owner->Climber->Form;P.SetBound(*Owner->Climber->Walker,Axis,false,P.Get(Axis));})]
   +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("set max"),[this,Axis](){auto& P=Owner->Climber->Form;P.SetBound(*Owner->Climber->Walker,Axis,true,P.Get(Axis));})]);
 }
 Add(SNew(SCheckBox).IsChecked_Lambda([this](){return Owner->Climber->Form.LockLimits?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
  .OnCheckStateChanged_Lambda([this](ECheckBoxState V){Owner->Climber->Form.LockLimits=V==ECheckBoxState::Checked;})[Text(TEXT("hold preview at the limits"),12)]);
 Add(SNew(SCheckBox).IsChecked_Lambda([this](){return Owner->Climber->Form.ShowLimits?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
  .OnCheckStateChanged_Lambda([this](ECheckBoxState V){Owner->Climber->Form.ShowLimits=V==ECheckBoxState::Checked;})[Text(TEXT("show permitted arcs"),12)]);
 Add(SNew(SHorizontalBox)
  +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("reset joint"),[this](){Owner->Climber->Form.ResetJoint(*Owner->Climber->Walker);})]
  +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("reset pose"),[this](){Owner->Climber->Form.Reset();})]);
 Add(SNew(SHorizontalBox)
  +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("copy other side"),[this](){Owner->Climber->Form.CopyOther(*Owner->Climber->Walker);})]
  +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("human defaults"),[this](){Owner->Climber->Form.Defaults(*Owner->Climber->Walker);RebuildForm();})]);
 Add(Text(TEXT("animation preview"),12,Gold));
 Add(SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&FormClips).InitiallySelectedItem(FormClips[F.Clip+1])
  .OnGenerateWidget_Lambda([this](TSharedPtr<FString> S){return Text(*S,12);})
  .OnSelectionChanged_Lambda([this](TSharedPtr<FString> S,ESelectInfo::Type){const int32 I=FormClips.IndexOfByKey(S);if(I>=0){Owner->Climber->Form.Clip=I-1;Owner->Climber->Form.Time=0;Owner->Climber->Form.Playing=false;}})
  [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(*FormClips[Owner->Climber->Form.Clip+1]);})]);
 Add(Button(TEXT("play / pause clip"),[this](){auto& P=Owner->Climber->Form;P.Playing=!P.Playing;}));
 Add(SNew(SSpinBox<float>).MinValue(0).MaxValue(60).Delta(.0333f).Value_Lambda([this](){return Owner->Climber->Form.Time;})
  .OnValueChanged_Lambda([this](float V){Owner->Climber->Form.Time=V;Owner->Climber->Form.Playing=false;}));
 Add(Text(TEXT("camera"),12,Gold));
 Add(SNew(SHorizontalBox)
  +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("left"),[this](){Owner->OrbitPlayerForm(-15,0,0);})]
  +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("right"),[this](){Owner->OrbitPlayerForm(15,0,0);})]
  +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("up"),[this](){Owner->OrbitPlayerForm(0,-10,0);})]
  +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("down"),[this](){Owner->OrbitPlayerForm(0,10,0);})]);
 Add(SNew(SHorizontalBox)
  +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("closer"),[this](){Owner->OrbitPlayerForm(0,0,-30);})]
  +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("farther"),[this](){Owner->OrbitPlayerForm(0,0,30);})]);
 Add(Text(TEXT("rig profiles · readable JSON"),12,Gold));
 Add(SNew(SEditableTextBox).Text(FText::FromString(F.Name)).HintText(FText::FromString(TEXT("profile name")))
  .OnTextChanged_Lambda([this](const FText& T){Owner->Climber->Form.Name=T.ToString().Left(80);}));
 Add(Button(TEXT("open profile folder"),[this](){Owner->ShowPlayerFormFiles();}));
 Add(Button(TEXT("refresh profile list"),[this](){RebuildForm();}));
 for(const FString& File:FPlayerForm::Versions())Add(Button(File,[this,File](){Owner->Climber->Form.Load(*Owner->Climber->Walker,File);RebuildForm();}));
 FormBody->SetContent(SNew(SVerticalBox)
  +SVerticalBox::Slot().AutoHeight()[Text(TEXT("RIG TUNING"),24)]
  +SVerticalBox::Slot().AutoHeight().Padding(0,5)[Text(TEXT("drag a dot to move it · drag a ring to rotate\nright-drag to orbit · scroll to zoom"),11,Gold)]
  +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().FillWidth(.45f)[Button(TEXT("apply rig"),[this](){Owner->ApplyPlayerForm();},true)]
   +SHorizontalBox::Slot().FillWidth(.35f)[Button(TEXT("save version"),[this](){Owner->Climber->Form.Save(*Owner->Climber->Walker);RebuildForm();})]
   +SHorizontalBox::Slot().FillWidth(.2f)[Button(TEXT("cancel"),[this](){Owner->TogglePlayerForm();})]]
  +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(STextBlock).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",10)).ColorAndOpacity(Muted).Text_Lambda([this](){return FText::FromString(Owner->Climber->Form.Status);})]
  +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Rows]]);
}
bool SRougarouUI::FormAxes(const FGeometry& G,FVector2f& Center,TArray<FVector2f>& Ends) const
{
 if(!Owner.IsValid()||!Owner->Climber||!Owner->Climber->Form.Open)return false;
 auto* Mesh=Owner->Climber->Walker.Get();const int32 I=Owner->Climber->Form.Selected;
 if(I<0)return false;
 const FName Bone=Mesh->GetBoneName(I);
 const FVector At=Mesh->GetBoneLocation(Bone);
 int32 W=1,H=1;Owner->GetViewportSize(W,H);
 auto Project=[&](FVector P,FVector2f& Out){FVector2D S;if(!Owner->ProjectWorldLocationToScreen(P,S,true))return false;Out=FVector2f(S.X*G.GetLocalSize().X/FMath::Max(W,1),S.Y*G.GetLocalSize().Y/FMath::Max(H,1));return true;};
 if(!Project(At,Center))return false;
 for(int32 Axis=0;Axis<3;++Axis){FVector2f End;if(!Project(At+Owner->Climber->Form.AxisWorld(*Mesh,Axis)*25,End))return false;Ends.Add(End);}
 return true;
}
FReply SRougarouUI::OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
 if(!Owner.IsValid()||!Owner->Climber||!Owner->Climber->Form.Open)return FReply::Unhandled();
 const FVector2f P=FVector2f(G.AbsoluteToLocal(E.GetScreenSpacePosition()));
 if(E.GetEffectingButton()==EKeys::RightMouseButton){OrbitDrag=true;DragStart=P;return FReply::Handled().CaptureMouse(SharedThis(this));}
 if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
 auto* Mesh=Owner->Climber->Walker.Get();int32 W=1,H=1;Owner->GetViewportSize(W,H);
 float Nearest=12;int32 Pick=INDEX_NONE;
 for(const auto& J:Owner->Climber->Form.Draft.JointLimits())
 {
  FVector2D V;if(!Owner->ProjectWorldLocationToScreen(Mesh->GetBoneLocation(Mesh->GetBoneName(J.Key)),V,true))continue;
  const FVector2f At(V.X*G.GetLocalSize().X/FMath::Max(W,1),V.Y*G.GetLocalSize().Y/FMath::Max(H,1));
  const float D=(P-At).Size();if(D<Nearest){Nearest=D;Pick=J.Key;}
 }
 if(Pick>=0)
 {
  auto& Form=Owner->Climber->Form;
  Form.Selected=Pick;
  RebuildForm();
  FVector Origin,Direction;
  if(!Owner->DeprojectScreenPositionToWorld(P.X*W/G.GetLocalSize().X,P.Y*H/G.GetLocalSize().Y,Origin,Direction)) return FReply::Handled();
  const FName Bone=Mesh->GetBoneName(Pick);
  AnchorNormal=Owner->PlayerCameraManager->GetCameraRotation().Vector();
  AnchorPlane=Mesh->GetBoneLocation(Bone);
  AnchorParent=Mesh->GetSocketTransform(Mesh->GetParentBone(Bone));
  AnchorStart=AnchorParent.InverseTransformPosition(FMath::LinePlaneIntersection(Origin,Origin+Direction,AnchorPlane,AnchorNormal))-Form.AnchorOffsets.FindRef(Pick);
  DragJoint=Pick;
  return FReply::Handled().CaptureMouse(SharedThis(this));
 }
 FVector2f Center;TArray<FVector2f> Ends;if(!FormAxes(G,Center,Ends))return FReply::Unhandled();
 float Closest=10;int32 AxisPick=-1;
 for(int32 Axis=0;Axis<3;++Axis)for(int32 I=0;I<100;++I)
 {
  const float A=2*PI*I/100;const FVector2f Q=Center+(Ends[(Axis+1)%3]-Center)*FMath::Cos(A)+(Ends[(Axis+2)%3]-Center)*FMath::Sin(A);
  const float D=(P-Q).Size();if(D<Closest){Closest=D;AxisPick=Axis;}
 }
 if(AxisPick<0)return FReply::Unhandled();
 DragSign=FVector::DotProduct(FVector::CrossProduct(Owner->Climber->Form.AxisWorld(*Mesh,(AxisPick+1)%3),Owner->Climber->Form.AxisWorld(*Mesh,(AxisPick+2)%3)),Owner->Climber->Form.AxisWorld(*Mesh,AxisPick))<0?-1.f:1.f;
 DragAxis=AxisPick;DragStart=P;DragCenter=Center;DragValue=Owner->Climber->Form.Get(DragAxis);
 DragU=Ends[(DragAxis+1)%3]-Center;DragV=Ends[(DragAxis+2)%3]-Center;
 const float Det=DragU.X*DragV.Y-DragU.Y*DragV.X;RingDrag=FMath::Abs(Det)>20;
 const FVector2f D=P-Center;
 DragAngle=RingDrag?FMath::Atan2((DragU.X*D.Y-DragU.Y*D.X)/Det,(D.X*DragV.Y-D.Y*DragV.X)/Det):0;
 return FReply::Handled().CaptureMouse(SharedThis(this));
}
FReply SRougarouUI::OnMouseMove(const FGeometry& G,const FPointerEvent& E)
{
 if(!HasMouseCapture())return FReply::Unhandled();
 if(OrbitDrag){const FVector2f Position=FVector2f(G.AbsoluteToLocal(E.GetScreenSpacePosition())),Movement=Position-DragStart;Owner->OrbitPlayerForm(-Movement.X*.3f,Movement.Y*.3f,0);DragStart=Position;return FReply::Handled();}
 if(DragJoint!=INDEX_NONE && Owner.IsValid() && Owner->Climber && Owner->Climber->Form.Open)
 {
  const FVector2D P=G.AbsoluteToLocal(E.GetScreenSpacePosition());
  int32 W=1,H=1;
  Owner->GetViewportSize(W,H);
  FVector Origin,Direction;
  if(Owner->DeprojectScreenPositionToWorld(P.X*W/G.GetLocalSize().X,P.Y*H/G.GetLocalSize().Y,Origin,Direction)
    && FMath::Abs(FVector::DotProduct(Direction,AnchorNormal))>.01)
  {
   const FVector Point=FMath::LinePlaneIntersection(Origin,Origin+Direction,AnchorPlane,AnchorNormal);
   Owner->Climber->Form.MoveAnchor(*Owner->Climber->Walker,AnchorParent.InverseTransformPosition(Point)-AnchorStart);
  }
  return FReply::Handled();
 }
 if(DragAxis<0)return FReply::Unhandled();
 const FVector2f P=FVector2f(G.AbsoluteToLocal(E.GetScreenSpacePosition())),D=P-DragCenter;
 float Amount=((P-DragStart).X-(P-DragStart).Y)*.5f;
 if(RingDrag)
 {
  const float Det=DragU.X*DragV.Y-DragU.Y*DragV.X;
  const float Angle=FMath::Atan2((DragU.X*D.Y-DragU.Y*D.X)/Det,(D.X*DragV.Y-D.Y*DragV.X)/Det);
  Amount=FMath::RadiansToDegrees(FMath::UnwindRadians(Angle-DragAngle));
 }
 Owner->Climber->Form.Set(*Owner->Climber->Walker,DragAxis,DragValue+Amount*DragSign);return FReply::Handled();
}
FReply SRougarouUI::OnMouseButtonUp(const FGeometry&,const FPointerEvent& E)
{
 if(OrbitDrag&&E.GetEffectingButton()==EKeys::RightMouseButton){OrbitDrag=false;return FReply::Handled().ReleaseMouseCapture();}
 if((DragAxis>=0||DragJoint!=INDEX_NONE) && E.GetEffectingButton()==EKeys::LeftMouseButton){DragAxis=-1;DragJoint=INDEX_NONE;return FReply::Handled().ReleaseMouseCapture();}
 return FReply::Unhandled();
}

FReply SRougarouUI::OnMouseWheel(const FGeometry&,const FPointerEvent& E)
{
 if(!Owner.IsValid()||!Owner->Climber||!Owner->Climber->Form.Open)return FReply::Unhandled();
 if(FormBody.IsValid()&&FormBody->GetCachedGeometry().IsUnderLocation(E.GetScreenSpacePosition()))return FReply::Unhandled();
 Owner->OrbitPlayerForm(0,0,-E.GetWheelDelta()*20);return FReply::Handled();
}
