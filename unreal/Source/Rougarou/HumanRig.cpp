#include "HumanRig.h"
#include "Rig.h"
#include "ReferenceSkeleton.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

float FHumanLimit::Reach(float Phi) const
{
 const float C=FMath::Cos(Phi), Sine=FMath::Sin(Phi);
 const float A=FMath::Max(.5f,C>=0?Forward:Back), B=FMath::Max(.5f,Sine>=0?Out:In);
 return FMath::Min(180.f,1/FMath::Sqrt(FMath::Square(C/A)+FMath::Square(Sine/B)));
}
// the turn that takes the bone from rest to Turned the anatomical way: flexion about F, then the shortest swing on (no turn about the bone yet)
static FQuat SwingPath(const FHumanFrame& Frame,const FVector& Turned)
{
 if(!Frame.Euler)return FQuat::FindBetweenNormals(Frame.T,Turned);
 const FVector D=(Frame.F^Frame.T).GetSafeNormal();
 const float Alpha=FMath::Atan2(FVector::DotProduct(Turned,D),FVector::DotProduct(Turned,Frame.T));
 const FVector T1=(Frame.T*FMath::Cos(Alpha)+D*FMath::Sin(Alpha)).GetSafeNormal();
 return FQuat::FindBetweenNormals(T1,Turned)*FQuat(Frame.F,Alpha);
}
FHumanAngles FHumanRig::Measure(FQuat Relative,const FHumanFrame& Frame)
{
 Relative.Normalize();
 const FVector D=(Frame.F^Frame.T).GetSafeNormal(), E=(Frame.S^Frame.T).GetSafeNormal();
 const FVector Turned=Relative.RotateVector(Frame.T).GetSafeNormal();
 FHumanAngles Result;
 Result.Swing=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Turned,Frame.T),-1.,1.)));
 Result.Bearing=FMath::Atan2(FVector::DotProduct(Turned,E),FVector::DotProduct(Turned,D));
 const FQuat Twist=SwingPath(Frame,Turned).Inverse()*Relative;
 const double Projection=Twist.X*Frame.T.X+Twist.Y*Frame.T.Y+Twist.Z*Frame.T.Z;
 Result.Twist=FMath::RadiansToDegrees(FMath::UnwindRadians(2*FMath::Atan2(Projection,Twist.W)))*Frame.Side;
 return Result;
}
FQuat FHumanRig::Clamp(FQuat Relative,const FHumanFrame& Frame,const FHumanLimit& Limit)
{
 const FHumanAngles M=Measure(Relative,Frame);
 const float Most=Limit.Reach(M.Bearing), Twist=FMath::Clamp(M.Twist,Limit.TwistMin,Limit.TwistMax);
 if(M.Swing<=Most+.0001f && FMath::IsNearlyEqual(Twist,M.Twist,.0001f))return Relative.GetNormalized();
 const float Angle=FMath::DegreesToRadians(FMath::Min(M.Swing,Most));
 const FVector D=(Frame.F^Frame.T).GetSafeNormal(), E=(Frame.S^Frame.T).GetSafeNormal();
 const FVector Direction=(Frame.T*FMath::Cos(Angle)+(D*FMath::Cos(M.Bearing)+E*FMath::Sin(M.Bearing))*FMath::Sin(Angle)).GetSafeNormal();
 return (SwingPath(Frame,Direction)*FQuat(Frame.T,FMath::DegreesToRadians(Twist/Frame.Side))).GetNormalized();
}
float FHumanRig::Violation(FQuat Relative,const FHumanFrame& Frame,const FHumanLimit& Limit)
{
 const FHumanAngles M=Measure(Relative,Frame);
 return FMath::Max3(0.f,M.Swing-Limit.Reach(M.Bearing),FMath::Max(Limit.TwistMin-M.Twist,M.Twist-Limit.TwistMax));
}
FHumanGoals FHumanGoals::FromSim(const FClimbSim& Sim,float Gap,float Inside,float HipDepth,float ChestDepth)
{
 FHumanGoals Result;
 const FClimbRoute& Route=Sim.Spec.Routes[Sim.Route];
 const FClimbPanel& Panel=Route.At(Sim.Position.Y);
 Result.Panels=Route.Panels;
 for(int32 L=0;L<4;L++)
 {
  const bool Arm=L>=2;
  const float Sign=L%2==0?-1.f:1.f;
  const bool Contact=(Sim.Grips[L]>=0 || Sim.Smeared[L]) && Sim.Arrived(L);
  const FClimbPanel& TipPanel=Route.At(Sim.Tips[L].Y);
  const float Depth=Arm?(ChestDepth>=0?ChestDepth:Gap-8):(HipDepth>=0?HipDepth:Gap-8);
  Result.Roots[L]=Route.Map(Sim.Joint(L),Depth+Inside*(Arm?26:30));
  const FVector At=Route.Map(Sim.Tips[L],Contact?3:Gap*.5f);
  const FClimbHold* Hold=Sim.Holds().IsValidIndex(Sim.Grips[L]) ? &Sim.Holds()[Sim.Grips[L]] : nullptr;
  const float Approach=Hold?1-FMath::SmoothStep(.025f,.22f,float(FVector2D::Distance(Sim.Tips[L],Sim.Spot(L)))):0.f;
  auto Placed=[&](const TOptional<FVector>& Center)
  {
   if(!Center.IsSet() || FMath::IsNearlyZero(Sim.Placement[L])) return Center;
   const FVector Offset=Route.Map(Sim.Spot(L))-Route.Map(Hold->Point);
   FVector Point=Center.GetValue()+Offset;
   if(Hold->Surface.IsValid()) Point-=Hold->Normal*Hold->Surface->Clear(Point);
   return TOptional<FVector>(Point);
  };
  FVector Over=TipPanel.Up;
  FString Grip=TEXT("free");
  if(Arm)
  {
   if(Hold)
   {
    Grip=Hold->Kind;
    Over=(-TipPanel.Right*Hold->Pull.X-TipPanel.Up*Hold->Pull.Y).GetSafeNormal();
    if(!Contact && Approach<=.01f)Grip=TEXT("free");
   }
   else if(Sim.Smeared[L]&&Contact)Grip=TEXT("smear");
   else if(Sim.Reaching==L && Sim.Holds().IsValidIndex(Sim.Aimed))
   {
    // lined up on a hold: the hand turns to its pull and shapes for it on the way in
    const FClimbHold& Next=Sim.Holds()[Sim.Aimed];
    Grip=Next.Kind;
    Over=(-TipPanel.Right*Next.Pull.X-TipPanel.Up*Next.Pull.Y).GetSafeNormal();
   }
   else Over=(At-Result.Roots[L]).GetSafeNormal();
   if(Contact && Hold)
   {
    Result.HandSurfaces[L-2]=Placed(Hold->KnuckleContact);
    // Reserve a palm-sized envelope, not just a point at the knuckles.
    // The center of a jug can protrude farther than its upper lip.
    if(Result.HandSurfaces[L-2].IsSet() && Hold->Surface.IsValid())
    {
     FVector ContactPoint=Result.HandSurfaces[L-2].GetValue();
     const FVector Side=(Over^TipPanel.Out).GetSafeNormal();
     float Lift=0;
     for(float Down : {0.f,3.f,6.f,9.f})
      for(float Across : {-2.4f,0.f,2.4f})
      {
       const FVector Palm=ContactPoint-Over*Down+Side*Across+TipPanel.Out*2.5f;
       Lift=FMath::Max(Lift,1.6f-Hold->Surface->Clear(Palm));
      }
     Result.HandSurfaces[L-2]=ContactPoint+TipPanel.Out*Lift;
    }
   }
   else if(Contact && Sim.Smeared[L])Result.HandSurfaces[L-2]=At-TipPanel.Out*3;
   Result.Ends[L]=At-Over*9+TipPanel.Out*(Grip==TEXT("crimp")?6.3f:4.6f);
   if(Hold && Hold->KnuckleContact.IsSet())
   {
    const FVector Wrist=Placed(Hold->KnuckleContact).GetValue()-Over*(Hold->Kind==TEXT("jug")?12.2f:10.f)+TipPanel.Out*2.2;
    Result.Ends[L]=FMath::Lerp(Result.Ends[L],Wrist+TipPanel.Out*(Contact?0.f:2.f),Contact?1.f:Approach);
   }
   Result.Along[L]=Over;
  }
  else
  {
   if(Contact && Hold)Result.ShoeSurfaces[L]=Placed(Hold->ShoeContact);
   else if(Contact && Sim.Smeared[L])Result.ShoeSurfaces[L]=At-TipPanel.Out*3;
   const FVector Shoe=Result.ShoeSurfaces[L].Get(Hold?FMath::Lerp(At,Placed(Hold->ShoeContact).Get(At),Approach):At);
   Result.Ends[L]=Shoe+TipPanel.Out*19+TipPanel.Up*4.7;
   Result.Along[L]=(Shoe-Result.Ends[L]).GetSafeNormal();
  }
  Result.PanelUp[L]=TipPanel.Up;Result.Out[L]=TipPanel.Out;Result.Grips[L]=Grip;Result.Held[L]=Contact;
  Result.Poles[L]=Arm ? Panel.Out*.6f+Panel.Right*(Sign*.35f)-Panel.Up*.8f
   : FRig::Knee(Panel.Right,Panel.Up,Panel.Out,Sign,Result.Ends[L]-Result.Roots[L]);
  if(!Arm && Sim.Reaching==L && !Contact)
  {
   const float High=FMath::SmoothStep(-35.f,-5.f,float(FVector::DotProduct(Result.Ends[L]-Result.Roots[L],Panel.Up)));
   // The raised knee leads the shoe, so it cannot swing down behind the heel.
   Result.Poles[L]=FMath::Lerp(Result.Poles[L],Panel.Up+Panel.Right*Sign*.25f-Panel.Out*.35f,High);
  }
  // A released limb hangs from its joint. The wall-relative reach targets still
  // apply when the player is aiming, travelling, or bearing on a contact.
  if(!Contact && !Hold && !Sim.Smeared[L] && Sim.Reaching!=L)
  {
   const float Span=Sim.Body.Lengths[L]*200;
   const FVector Swing=Panel.Right*(Sign*(Arm?5.f:3.f))+Panel.Out*(Arm?3.f:5.f);
   const FVector Hang=(FVector::DownVector*Span+Swing).GetSafeNormal();
   Result.Ends[L]=Result.Roots[L]+Hang*Span*(Arm?.92f:.96f);
   Result.Along[L]=Arm?Hang:(-TipPanel.Out+FVector::DownVector*.65f).GetSafeNormal();
   Result.Poles[L]=Arm?Panel.Out+Panel.Right*Sign*.2f:-Panel.Out+Panel.Right*Sign*.15f;
  }
 }
 Result.Look=-Panel.Out;
 for(int32 L=0;L<4;L++)
 {
  Result.Weight[L]=Sim.Loads[L]/FMath::Max(1.f,Sim.Spec.Mass*Sim.Spec.Gravity);
  Result.Want[L]=Result.Held[L]?0.f:Sim.Latch[L]<0?1.f:Sim.Reaching==L?.7f:0.f;
 }
 for(int32 H=0;H<Sim.Holds().Num();++H)
 {
  const FClimbHold& Hold=Sim.Holds()[H];
  if(Hold.Radius<=0)continue;
  FHumanGoals::FDome Dome{Hold.Center,Hold.Normal,Hold.Radius,Hold.Height,0,Hold.Surface};
  for(int32 L=0;L<4;++L)if(Sim.Grips[L]==H)Dome.Owners|=1<<L;
  Result.Domes.Add(Dome);
 }
 return Result;
}
float FHumanPose::Cost(const FHumanGoals& Goals) const
{
 if(!Finite)return 1.e9f;
 float Result=Comfort*.12f+Goals.RelaxedSupport*(SupportEffort+.0005f*Shift.SizeSquared());
 // a held limb must stay on its hold. a limb being placed counts for a good deal too: when a hip or shoulder runs out of
 // range short of where the foot or hand is going, it is the pelvis that has to turn, tip and come off the wall to let it
 for(int32 L=0;L<4;L++)Result+=FMath::Square(Miss[L])*(Goals.Held[L]?10.f:FMath::Lerp(.15f,2.f,Goals.Want[L]));
 Result+=FMath::Square(FMath::Max(0.f,2-MinClearance))*50;
 return Result;
}
FString FHumanRig::DefaultProfilePath(){return FPaths::ProjectContentDir()/TEXT("Data/human_motion.json");}
bool FHumanRig::ValidProfile(const FString& Json)
{
 TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Map=nullptr;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root.IsValid()||!Root->TryGetObjectField(TEXT("limits"),Map))return false;
 if(Root->HasField(TEXT("anchor_offsets_cm")))
 {
  const TSharedPtr<FJsonObject>* Anchors=nullptr;
  if(!Root->TryGetObjectField(TEXT("anchor_offsets_cm"),Anchors)) return false;
  for(const auto& Pair:(*Anchors)->Values)
  {
   const TArray<TSharedPtr<FJsonValue>>* XYZ=nullptr;
   if(!Pair.Value->TryGetArray(XYZ)||XYZ->Num()!=3) return false;
   FVector Offset;
   for(int32 Axis=0;Axis<3;++Axis)
    if(!(*XYZ)[Axis]->TryGetNumber(Offset[Axis])||!FMath::IsFinite(Offset[Axis])) return false;
   if(Offset.SizeSquared()>64.001) return false;
  }
 }
 // Every supported joint is required; partial or malformed profiles cannot
 // silently remove a constraint from the live solver.
 const TCHAR* Required[]={TEXT("Head"),TEXT("Waist"),TEXT("Spine01"),TEXT("Spine02"),TEXT("NeckTwist01"),TEXT("L_Clavicle"),TEXT("R_Clavicle"),TEXT("L_Upperarm"),TEXT("R_Upperarm"),TEXT("L_Forearm"),TEXT("R_Forearm"),TEXT("L_Hand"),TEXT("R_Hand"),TEXT("L_Thigh"),TEXT("R_Thigh"),TEXT("L_Calf"),TEXT("R_Calf"),TEXT("L_Foot"),TEXT("R_Foot")};
 for(const TCHAR* Key:Required)
 {
  const TSharedPtr<FJsonObject>* J=nullptr;if(!(*Map)->TryGetObjectField(Key,J))return false;
  for(const TCHAR* Field:{TEXT("fwd"),TEXT("back"),TEXT("out"),TEXT("inn")}){double V;if(!(*J)->TryGetNumberField(Field,V)||!FMath::IsFinite(V)||V<0||V>180)return false;}
  const TArray<TSharedPtr<FJsonValue>>* T=nullptr;double A,B;
  if(!(*J)->TryGetArrayField(TEXT("twist"),T)||T->Num()!=2||!(*T)[0]->TryGetNumber(A)||!(*T)[1]->TryGetNumber(B)||!FMath::IsFinite(A)||!FMath::IsFinite(B)||A< -180||B>180||A>B)return false;
 }
 return true;
}
FString FHumanRig::ProfilePath()
{
 const FString Active=FPaths::ProjectSavedDir()/TEXT("RigProfiles/active.json");FString Json;
 return FFileHelper::LoadFileToString(Json,*Active)&&ValidProfile(Json)?Active:DefaultProfilePath();
}
bool FHumanRig::SetJointLimit(int32 I,const FHumanLimit& V)
{
 if(!Limits.Contains(I))return false;
 for(float X:{V.Forward,V.Back,V.Out,V.In})if(!FMath::IsFinite(X)||X<0||X>180)return false;
 if(!FMath::IsFinite(V.TwistMin)||!FMath::IsFinite(V.TwistMax)||V.TwistMin< -180||V.TwistMax>180||V.TwistMin>V.TwistMax)return false;
 Limits[I]=V;Reset();return true;
}
FQuat FHumanRig::PreviewJoint(int32 I,FVector Degrees,bool Restrict) const
{
 const FHumanFrame* F=Frames.Find(I);const FHumanLimit* L=Limits.Find(I);
 if(!F||!L)return Neutral.IsValidIndex(I)?Neutral[I].GetRotation():FQuat::Identity;
 const float Angle=FMath::Min(180.f,float(FVector2D(Degrees.X,Degrees.Y).Size()));
 const float Bearing=FMath::Atan2(Degrees.Y,Degrees.X),R=FMath::DegreesToRadians(Angle);
 const FVector D=(F->F^F->T).GetSafeNormal(),E=(F->S^F->T).GetSafeNormal();
 const FVector Turn=F->T*FMath::Cos(R)+(D*FMath::Cos(Bearing)+E*FMath::Sin(Bearing))*FMath::Sin(R);
 FQuat Relative=SwingPath(*F,Turn)*FQuat(F->T,FMath::DegreesToRadians(Degrees.Z/F->Side));
 if(Restrict)Relative=Clamp(Relative,*F,*L);
 return (Neutral[I].GetRotation()*Relative).GetNormalized();
}
int32 FHumanRig::Bone(const TCHAR* Name) const
{
 const int32* Index=Indices.Find(FName(Name));return Index?*Index:INDEX_NONE;
}
void FHumanRig::Compose(const TArray<FTransform>& Local,TArray<FTransform>& Space) const
{
 Space.SetNumUninitialized(Local.Num());
 for(int32 I=0;I<Local.Num();I++)Space[I]=Parents[I]<0?Local[I]:Local[I]*Space[Parents[I]];
}
void FHumanRig::Reset(){HasHistory=false;Push=FVector::ZeroVector;Pitch=0;Yaw=0;Previous.Reset();Shift=FVector::ZeroVector;for(float& A:Swivel)A=0;}

bool FHumanRig::Initialize(const FReferenceSkeleton& Ref,const FString& Profile)
{
 Names.Reset();Indices.Reset();Parents.Reset();Limits.Reset();Frames.Reset();Aliases.Reset();Reset();
 Rest=Ref.GetRefBonePose();
 for(int32 I=0;I<Rest.Num();I++){Names.Add(Ref.GetBoneName(I));Indices.Add(Names[I],I);Parents.Add(Ref.GetParentIndex(I));}
 const TCHAR* Required[]={TEXT("Hips"),TEXT("Spine"),TEXT("Spine1"),TEXT("Spine2"),TEXT("Head"),TEXT("LeftArm"),TEXT("RightArm"),TEXT("LeftForeArm"),TEXT("RightForeArm"),TEXT("LeftHand"),TEXT("RightHand"),TEXT("LeftUpLeg"),TEXT("RightUpLeg"),TEXT("LeftLeg"),TEXT("RightLeg"),TEXT("LeftFoot"),TEXT("RightFoot"),TEXT("LeftToeBase"),TEXT("RightToeBase")};
 for(const TCHAR* Name:Required)if(Bone(Name)<0){Rest.Reset();return false;}
 Branches.Reset(Rest.Num());
 Branches.SetNum(Rest.Num());
 SlowJoints.SetNumUninitialized(Rest.Num());
 for(int32 I=0;I<Rest.Num();++I)
 {
  SlowJoints[I]=Names[I].ToString().StartsWith(TEXT("Spine"));
  for(int32 Parent=I;Parent>=0;Parent=Parents[Parent])Branches[Parent].Add(I);
 }
 FString Json;TSharedPtr<FJsonObject> Data;
 if(FFileHelper::LoadFileToString(Json,*Profile)&&ValidProfile(Json))FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data);
 const TSharedPtr<FJsonObject>* Anchors=nullptr;
 if(Data.IsValid()&&Data->TryGetObjectField(TEXT("anchor_offsets_cm"),Anchors))
  for(const auto& Pair:(*Anchors)->Values)
  {
   const int32 Index=Bone(*Pair.Key);
   if(Index<=0||Names[Index]==TEXT("Hips")) continue;
   const auto& XYZ=Pair.Value->AsArray();
   Rest[Index].AddToTranslation(FVector(XYZ[0]->AsNumber(),XYZ[1]->AsNumber(),XYZ[2]->AsNumber()));
  }
 Compose(Rest,RestSpace);
 auto P=[&](const TCHAR* N){return RestSpace[Bone(N)].GetLocation();};
 RefPelvis=(P(TEXT("LeftUpLeg"))+P(TEXT("RightUpLeg")))/2;
 RefUp=((P(TEXT("LeftArm"))+P(TEXT("RightArm")))/2-RefPelvis).GetSafeNormal();
 RefSide=(P(TEXT("LeftArm"))-P(TEXT("RightArm"))).GetSafeNormal();
 RefFront=FVector::VectorPlaneProject(P(TEXT("LeftToeBase"))-P(TEXT("LeftFoot"))+P(TEXT("RightToeBase"))-P(TEXT("RightFoot")),RefUp).GetSafeNormal();
 Neutral=Rest;
 float Drop=32;
 if(Data.IsValid())
 {
  const TSharedPtr<FJsonObject>* Native=nullptr;
  if(Data->TryGetObjectField(TEXT("native"),Native))
  {
   double V;
   if((*Native)->TryGetNumberField(TEXT("arm_rest_drop_degrees"),V))Drop=FMath::Clamp(float(V),0.f,60.f);
   if((*Native)->TryGetNumberField(TEXT("max_body_shift_cm"),V))MaxShift=FMath::Clamp(float(V),0.f,20.f);
   if((*Native)->TryGetNumberField(TEXT("joint_rate_radians"),V))JointRate=FMath::Clamp(float(V),1.f,5.f);
   if((*Native)->TryGetNumberField(TEXT("swivel_rate_radians"),V))SwivelRate=FMath::Clamp(float(V),1.f,2.f);
  }
  const TSharedPtr<FJsonObject>* M=nullptr;
  if(Data->TryGetObjectField(TEXT("native_mix"),M))
  {
   double V;
   if((*M)->TryGetNumberField(TEXT("waist"),V))Mix.Waist=FMath::Clamp(float(V),.05f,.8f);
   if((*M)->TryGetNumberField(TEXT("spine"),V))Mix.Spine=FMath::Clamp(float(V),Mix.Waist+.05f,.95f);
   if((*M)->TryGetNumberField(TEXT("neck"),V))Mix.Neck=FMath::Clamp(float(V),.15f,.85f);
   if((*M)->TryGetNumberField(TEXT("scapula"),V))Mix.Scapula=FMath::Clamp(float(V),.05f,.4f);
  }
 }
 // Use an anatomical neutral independent of whether the imported bind is A or T.
 // No geometry or bind offsets change.
 for(const TCHAR* Side:{TEXT("Left"),TEXT("Right")})
 {
  const int32 I=Bone(*(FString(Side)+TEXT("Arm"))), Child=Bone(*(FString(Side)+TEXT("ForeArm")));
  const FVector Axis=(RestSpace[Child].GetLocation()-RestSpace[I].GetLocation()).GetSafeNormal();
  const FVector Lateral=FVector::VectorPlaneProject(Axis,RefUp).GetSafeNormal();
  const FVector Desired=(Lateral*FMath::Cos(FMath::DegreesToRadians(Drop))-RefUp*FMath::Sin(FMath::DegreesToRadians(Drop))).GetSafeNormal();
  const FQuat Global=FQuat::FindBetweenNormals(Axis,Desired)*RestSpace[I].GetRotation();
  Neutral[I].SetRotation((RestSpace[Parents[I]].GetRotation().Inverse()*Global).GetNormalized());
 }
 Compose(Neutral,NeutralSpace);
 const TPair<const TCHAR*,const TCHAR*> Trunk[]={{TEXT("Waist"),TEXT("Spine")},{TEXT("Spine01"),TEXT("Spine1")},{TEXT("Spine02"),TEXT("Spine2")},{TEXT("NeckTwist01"),TEXT("Neck")},{TEXT("Head"),TEXT("Head")}};
 for(auto Pair:Trunk)Aliases.Add(Pair.Key,Pair.Value);
 const TPair<const TCHAR*,const TCHAR*> Parts[]={{TEXT("Clavicle"),TEXT("Shoulder")},{TEXT("Upperarm"),TEXT("Arm")},{TEXT("Forearm"),TEXT("ForeArm")},{TEXT("Hand"),TEXT("Hand")},{TEXT("Thigh"),TEXT("UpLeg")},{TEXT("Calf"),TEXT("Leg")},{TEXT("Foot"),TEXT("Foot")}};
 for(const TCHAR* Side:{TEXT("R"),TEXT("L")})for(auto Part:Parts)Aliases.Add(*(FString(Side)+TEXT("_")+Part.Key),*((FString(Side)==TEXT("R")?TEXT("Right"):TEXT("Left"))+FString(Part.Value)));
 TMap<FName,FName> Points;
 Points.Add(TEXT("Spine"),TEXT("Spine1"));Points.Add(TEXT("Spine1"),TEXT("Spine2"));Points.Add(TEXT("Spine2"),TEXT("Neck"));Points.Add(TEXT("Neck"),TEXT("Head"));
 for(const TCHAR* Side:{TEXT("Left"),TEXT("Right")})
 {
  const FString Prefix(Side);
  for(auto Pair:TArray<TPair<FString,FString>>{{TEXT("Shoulder"),TEXT("Arm")},{TEXT("Arm"),TEXT("ForeArm")},{TEXT("ForeArm"),TEXT("Hand")},{TEXT("Hand"),TEXT("HandMiddle1")},{TEXT("UpLeg"),TEXT("Leg")},{TEXT("Leg"),TEXT("Foot")},{TEXT("Foot"),TEXT("ToeBase")}})Points.Add(*(Prefix+Pair.Key),*(Prefix+Pair.Value));
 }
 const TSharedPtr<FJsonObject>* FileLimits=nullptr;
 if(!Data.IsValid()||!Data->TryGetObjectField(TEXT("limits"),FileLimits)){Rest.Reset();return false;}
 for(const auto& Alias:Aliases)
 {
  const int32 I=Bone(*Alias.Value.ToString());if(I<0)continue;
  const TSharedPtr<FJsonObject>* L=nullptr;if(!(*FileLimits)->TryGetObjectField(Alias.Key.ToString(),L))continue;
  FHumanLimit Bound;
  Bound.Forward=(*L)->GetNumberField(TEXT("fwd"));Bound.Back=(*L)->GetNumberField(TEXT("back"));Bound.Out=(*L)->GetNumberField(TEXT("out"));Bound.In=(*L)->GetNumberField(TEXT("inn"));
  const auto& Twist=(*L)->GetArrayField(TEXT("twist"));Bound.TwistMin=Twist[0]->AsNumber();Bound.TwistMax=Twist[1]->AsNumber();Limits.Add(I,Bound);
  const FName* NextName=Points.Find(Alias.Value);const int32 Next=NextName?Bone(*NextName->ToString()):INDEX_NONE;
  FVector T=Next>=0?NeutralSpace[Next].GetLocation()-NeutralSpace[I].GetLocation():NeutralSpace[I].GetLocation()-NeutralSpace[Parents[I]].GetLocation();T.Normalize();
  FVector Front=RefFront;if(FMath::Abs(FVector::DotProduct(T,Front))>.9)Front=RefUp;
  const FVector F=(T^Front).GetSafeNormal();FVector S=(T^F).GetSafeNormal();
  const bool Left=Alias.Key.ToString().StartsWith(TEXT("L_"));
  const bool Arm=Alias.Key.ToString().Contains(TEXT("Upperarm"))||Alias.Key.ToString().Contains(TEXT("Clavicle"));
  const FVector Outward=Arm?RefUp:Alias.Key.ToString().Contains(TEXT("Thigh"))||Alias.Key.ToString().Contains(TEXT("Calf"))?RefSide*(Left?1:-1):RefSide;
  if(FVector::DotProduct(S^T,Outward)<0)S=-S;
  const FQuat Inv=NeutralSpace[I].GetRotation().Inverse();
   FHumanFrame Built{Inv.RotateVector(T),Inv.RotateVector(F),Inv.RotateVector(S),Left?-1.f:1.f};
  Built.Euler=Alias.Key.ToString().EndsWith(TEXT("Thigh"))||Alias.Key.ToString().EndsWith(TEXT("Upperarm"));
  Frames.Add(I,Built);
 }
 return true;
}
FQuat FHumanRig::LimitLocal(int32 Index,FQuat Local) const
{
 const FHumanLimit* L=Limits.Find(Index);const FHumanFrame* Frame=Frames.Find(Index);
 if(!LimitsEnabled||!L||!Frame)return Local.GetNormalized();
 return (Neutral[Index].GetRotation()*Clamp(Neutral[Index].GetRotation().Inverse()*Local,*Frame,*L)).GetNormalized();
}

// how far a ball is clear of a hold's resin dome (negative inside), and the way out along the dome's gradient
static float HumanDomeClear(const FHumanGoals::FDome& D,FVector P,float Radius,FVector& Way)
{
 const FVector Rel=P-D.C;const float Along=FVector::DotProduct(Rel,D.N);
 if(Along< -Radius)return 99;
 const FVector Lateral=Rel-D.N*Along;
 const float A=D.R+Radius,B=D.H+Radius,Q=FMath::Square(Lateral.Size()/A)+FMath::Square(FMath::Max(Along,0.f)/B);
 if(Q>=1)return 99;
 Way=(Lateral/(A*A)+D.N*(FMath::Max(Along,0.f)/(B*B))).GetSafeNormal();
 if(Way.IsNearlyZero())Way=D.N;
 return (FMath::Sqrt(Q)-1)*FMath::Min(A,B);
}
static float HumanWallDepth(const FHumanGoals& Goals,FVector Point,FVector* Normal=nullptr)
{
 // the nearest panel strip in front of the point; past a strip's end, the distance to its edge
 float Result=MAX_flt,Best=MAX_flt;
 for(const FClimbPanel& P:Goals.Panels)
 {
  const FVector D=Point-P.Origin;
  const float Along=FVector::DotProduct(D,P.Up),Clamped=FMath::Clamp(Along,0.f,P.Length*100);
  const FVector Q=D-P.Up*Clamped;
  const float Here=FVector::DotProduct(Q,P.Out);
  if(Here<-60)continue;
  const float Distance=Clamped==Along?FMath::Abs(Here):Q.Size();
  if(Distance<Best){Best=Distance;Result=Clamped==Along?Here:Distance;if(Normal)*Normal=P.Out;}
 }
 return Result;
}
FHumanPose FHumanRig::Pass(const FHumanGoals& Goals,FVector Offset,const float* Swivels,bool Search,float* Chosen,float PelvisPitch,float PelvisYaw,bool FitSoles)
{
 FHumanPose Result;Result.Local=Rest;Result.Shift=Offset;Result.PelvisPitch=PelvisPitch;Result.PelvisYaw=PelvisYaw;
 TArray<FTransform> Space=RestSpace;
 const FVector Pelvis=(Goals.Roots[0]+Goals.Roots[1])/2, Chest=(Goals.Roots[2]+Goals.Roots[3])/2;
 const FVector Up=(Chest-Pelvis).GetSafeNormal();
 const FQuat From=FRotationMatrix::MakeFromZX(RefUp,RefSide).ToQuat();
 const FQuat To=FRotationMatrix::MakeFromZX(Up,Goals.Roots[3]-Goals.Roots[2]).ToQuat();
 const FQuat Base=(To*From.Inverse()).GetNormalized();
 const FQuat PelvisTurn=FQuat(RefUp,PelvisYaw)*FQuat::FindBetweenNormals(RefUp,(RefUp+RefFront*FMath::Tan(PelvisPitch)).GetSafeNormal());
 const FQuat Rotation=(Base*PelvisTurn).GetNormalized();
 Result.World=FTransform(Rotation,Pelvis+Offset-Rotation.RotateVector(RefPelvis));
 auto UpdateBranch=[&](int32 Root)
 {
  for(int32 I:Branches[Root])
   Space[I]=Parents[I]<0?Result.Local[I]:Result.Local[I]*Space[Parents[I]];
 };
 auto Put=[&](int32 I,FQuat Global)
 {
  if(I<0)return;
  const FQuat Parent=Parents[I]>=0?Space[Parents[I]].GetRotation():FQuat::Identity;
  Result.Local[I].SetRotation(LimitLocal(I,Parent.Inverse()*Global));UpdateBranch(I);
 };
 float Lift[2];for(int32 A=0;A<2;A++)Lift[A]=FMath::Clamp(float(FVector::DotProduct(Goals.Ends[A+2]-Goals.Roots[A+2],Up)/50),0.f,1.f);
 const float ExtensionDegrees=Goals.Out[2].Z < -.05f ? 12.f : 4.f;
 const float Extension=FMath::DegreesToRadians(ExtensionDegrees*FMath::Min(1.f,(Lift[0]+Lift[1])*.55f));
 // the chest leans toward a reaching arm, but only a little: more lifts that whole shoulder up and out
 const float Lateral=FMath::DegreesToRadians(6.f*(Lift[0]-Lift[1]));
 const FVector ChestUp=(RefUp-RefFront*FMath::Tan(Extension)+RefSide*FMath::Tan(Lateral)).GetSafeNormal();
 const FQuat ChestTurn=PelvisTurn.Inverse()*FQuat::FindBetweenNormals(RefUp,ChestUp);
 const TPair<int32,float> SpineWeights[]={{Bone(TEXT("Spine")),Mix.Waist},{Bone(TEXT("Spine1")),Mix.Spine},{Bone(TEXT("Spine2")),1}};
 for(const auto& Pair:SpineWeights)
  Put(Pair.Key,FQuat::Slerp(FQuat::Identity,ChestTurn,Pair.Value)*RestSpace[Pair.Key].GetRotation());
 const FVector Look=Result.World.InverseTransformVectorNoScale(Goals.Look).GetSafeNormal();
 const FQuat HeadTurn=Look.IsNearlyZero()?ChestTurn:FQuat::FindBetweenNormals(RefFront,Look);
 const int32 Neck=Bone(TEXT("Neck")), Head=Bone(TEXT("Head"));
 if(Neck>=0)Put(Neck,FQuat::Slerp(ChestTurn,HeadTurn,Mix.Neck)*RestSpace[Neck].GetRotation());
 Put(Head,HeadTurn*RestSpace[Head].GetRotation());
 const TCHAR* Upper[]={TEXT("RightUpLeg"),TEXT("LeftUpLeg"),TEXT("RightArm"),TEXT("LeftArm")};
 const TCHAR* Lower[]={TEXT("RightLeg"),TEXT("LeftLeg"),TEXT("RightForeArm"),TEXT("LeftForeArm")};
 const TCHAR* End[]={TEXT("RightFoot"),TEXT("LeftFoot"),TEXT("RightHand"),TEXT("LeftHand")};
 const TCHAR* Tip[]={TEXT("RightToeBase"),TEXT("LeftToeBase"),TEXT("RightHandMiddle1"),TEXT("LeftHandMiddle1")};
 for(int32 Limb=0;Limb<4;Limb++)
 {
  const bool Arm=Limb>=2, Left=Limb%2==1;
  const int32 U=Bone(Upper[Limb]), L=Bone(Lower[Limb]), E=Bone(End[Limb]), TipIndex=Bone(Tip[Limb]);
  FVector Target=Result.World.InverseTransformPosition(Goals.Ends[Limb]);
  if(Arm)
  {
   const int32 Clavicle=Bone(Left?TEXT("LeftShoulder"):TEXT("RightShoulder"));
   if(Clavicle>=0)
   {
    const FVector Direction=(Target-Space[U].GetLocation()).GetSafeNormal();
    // the collarbone rises fully only when the arm is stretched toward its end (a straight-arm hang needs that reach);
    // a bent arm overhead keeps the shoulder down, and at rest it sits a touch lower still, so raised arms do not
    // push the deltoids up and out into a v
    const float Span=(Rest[L].GetTranslation()*Space[U].GetScale3D()).Size()+(Rest[E].GetTranslation()*Space[U].GetScale3D()).Size();
    const float Stretched=FMath::SmoothStep(.8f,1.f,float(FVector::Distance(Target,Space[U].GetLocation())/FMath::Max(Span,1.f)));
    const float Angle=FMath::Min(FMath::Asin(FMath::Clamp(FVector::DotProduct(Direction,RefUp),0.,1.))*Mix.Scapula*FMath::Lerp(.2f,.4f,Stretched),FMath::DegreesToRadians(FMath::Lerp(4.f,8.f,Stretched)))
     -FMath::DegreesToRadians(4.f)*(1-Stretched);
    const FVector Axis=(RestSpace[U].GetLocation()-RestSpace[Clavicle].GetLocation()).GetSafeNormal();
    const FVector Desired=(Axis*FMath::Cos(Angle)+RefUp*FMath::Sin(Angle)).GetSafeNormal();
    const FQuat Carried=Space[Parents[Clavicle]].GetRotation()*RestSpace[Parents[Clavicle]].GetRotation().Inverse();
    Put(Clavicle,Carried*FQuat::FindBetweenNormals(Axis,Desired)*RestSpace[Clavicle].GetRotation());
   }
  }
  const bool PlantSole=FitSoles && !Arm && Goals.ShoeSurfaces[Limb].IsSet();
  const int32 SoleBone=Arm?INDEX_NONE:Bone(Left?TEXT("LeftSoleContact"):TEXT("RightSoleContact"));
  const FVector Sole=SoleBone>=0?RestSpace[SoleBone].GetLocation()-RestSpace[E].GetLocation():RefFront*19-RefUp*4.7;
  const FVector TargetSole=PlantSole?Result.World.InverseTransformPosition(Goals.ShoeSurfaces[Limb].GetValue()):FVector::ZeroVector;
  if(Arm && Goals.HandSurfaces[Limb-2].IsSet())
  {
   const FVector ContactAlong=Result.World.InverseTransformVectorNoScale(Goals.Along[Limb]);
   const FVector Facing=Result.World.InverseTransformVectorNoScale(-Goals.Out[Limb]);
   const FVector KnuckleOffset=RestSpace[TipIndex].GetLocation()-RestSpace[E].GetLocation();
   const FQuat Orientation=FRig::Orient(RestSpace[E].GetRotation(),KnuckleOffset,-RefUp,ContactAlong,Facing);
   Target=Result.World.InverseTransformPosition(Goals.HandSurfaces[Limb-2].GetValue()+Goals.Out[Limb]*2.5f)
    -Orientation.RotateVector(RestSpace[E].GetRotation().Inverse().RotateVector(KnuckleOffset));
  }
  float SmallestMiss=MAX_flt;
  FQuat BestLeg[3];
  for(int32 Iteration=0;Iteration<(PlantSole?7:1);++Iteration)
  {
   const FVector Start=Space[U].GetLocation();
   const FVector Axis=(Target-Start).GetSafeNormal(1.e-8,FVector::DownVector);
   const FVector UpperOffset=Rest[L].GetTranslation()*Space[U].GetScale3D(), LowerOffset=Rest[E].GetTranslation()*Space[L].GetScale3D();
   const float One=UpperOffset.Size(), Two=LowerOffset.Size();
   FVector Pole=FVector::VectorPlaneProject(Result.World.InverseTransformVectorNoScale(Goals.Poles[Limb]),Axis).GetSafeNormal();
   if(Pole.IsNearlyZero())Pole=(Axis^RefUp).GetSafeNormal();
   if(Pole.IsNearlyZero())Pole=(Axis^RefSide).GetSafeNormal();
   const FVector Around=Axis^Pole;
   const float Distance=FMath::Clamp(float((Target-Start).Size()),FMath::Abs(One-Two)+.001f,One+Two-.001f);
   const float Along=(One*One+Distance*Distance-Two*Two)/(2*Distance);
   const float Height=FMath::Sqrt(FMath::Max(0.f,One*One-Along*Along));
   const FVector Wrist=Start+Axis*Distance;
   const FQuat Parent=Space[Parents[U]].GetRotation();
   const FQuat ParentInverse=Parent.Inverse();
   const FQuat UpperRest=RestSpace[U].GetRotation();
   const FQuat UpperRestInverse=UpperRest.Inverse();
   const FQuat LowerRest=RestSpace[L].GetRotation();
   const FQuat Carried=Parent*RestSpace[Parents[U]].GetRotation().Inverse();
   const FVector RestAxis=(RestSpace[L].GetLocation()-RestSpace[U].GetLocation()).GetSafeNormal();
   const FVector RestLower=(RestSpace[E].GetLocation()-RestSpace[L].GetLocation()).GetSafeNormal();
   const FVector Hinge=(RestAxis^(Arm?RefFront:-RefFront)).GetSafeNormal();
   struct FCandidate{FQuat Upper,Lower;FVector Middle,Finish;float Cost=0;};
   const FVector CarriedAxis=Carried.RotateVector(RestAxis);
   const FVector LocalHinge=UpperRestInverse.RotateVector(Hinge);
   const FVector LowerDirection=LowerOffset.GetSafeNormal();
   auto Attempt=[&](float Psi)
   {
    FCandidate C;
    const FVector Middle=Start+Axis*Along+(Pole*FMath::Cos(Psi)+Around*FMath::Sin(Psi))*Height;
    const FVector D1=(Middle-Start).GetSafeNormal(), D2=(Wrist-Middle).GetSafeNormal();
    FQuat QU=FQuat::FindBetweenNormals(CarriedAxis,D1)*Carried*UpperRest;
    const FVector Need=D1^D2;const float Sine=Need.Size();
    if(Sine>.02f)
    {
     const FVector Here=FVector::VectorPlaneProject(QU.RotateVector(LocalHinge),D1).GetSafeNormal();
     const FVector Normal=Need.GetSafeNormal();
     const float Roll=FMath::Atan2(FVector::DotProduct(D1,Here^Normal),FVector::DotProduct(Here,Normal))*FMath::Clamp((Sine-.02f)/.3f,0.f,1.f);
     QU=FQuat(D1,Roll)*QU;
    }
    const FQuat CarryLower=QU*UpperRestInverse;
    const FQuat QL=FQuat::FindBetweenNormals(CarryLower.RotateVector(RestLower),D2)*CarryLower*LowerRest;
    const FQuat LU=ParentInverse*QU, LL=QU.Inverse()*QL;
    C.Upper=LimitLocal(U,LU);
    const FQuat ActualU=Parent*C.Upper;
    C.Middle=Start+ActualU.RotateVector(UpperOffset);
    // Re-aim the lower segment after constraining the shoulder/hip. Otherwise
    // a legal upper-joint correction unnecessarily pulls the contact away too.
    const FQuat LowerCarried=ActualU*UpperRestInverse*LowerRest;
    const FVector CurrentLower=LowerCarried.RotateVector(LowerDirection);
    const FVector WantedLower=(Target-C.Middle).GetSafeNormal();
    const FQuat LowerAimed=FQuat::FindBetweenNormals(CurrentLower,WantedLower)*LowerCarried;
    C.Lower=LimitLocal(L,ActualU.Inverse()*LowerAimed);
    const FQuat Original=LimitLocal(L,LL);
    auto LowerCost=[&](FQuat Q){const FVector EndPoint=C.Middle+(ActualU*Q).RotateVector(LowerOffset);const float Depth=HumanWallDepth(Goals,Result.World.TransformPosition((C.Middle+EndPoint)/2));return (EndPoint-Target).SizeSquared()*.3+100*FMath::Square(FMath::Max(0.f,(Arm?4.f:6.f)-Depth));};
    if(LowerCost(Original)<LowerCost(C.Lower))C.Lower=Original;
    const FQuat ActualL=ActualU*C.Lower;
    C.Finish=C.Middle+ActualL.RotateVector(LowerOffset);
    C.Cost=FMath::Square(float((C.Finish-Target).Size()))*.3f+.5f*FMath::Square(Psi/PI);
    const TPair<int32,FQuat> JointRotations[]={{U,LU},{L,LL}};
    for(const auto& Pair:JointRotations)
    {
     const FHumanLimit* Bound=Limits.Find(Pair.Key);const FHumanFrame* Frame=Frames.Find(Pair.Key);
     if(!LimitsEnabled||!Bound||!Frame)continue;
     const FHumanAngles M=Measure(Neutral[Pair.Key].GetRotation().Inverse()*Pair.Value,*Frame);
     const float R=M.Swing/Bound->Reach(M.Bearing), T=M.Twist>=0?M.Twist/FMath::Max(1.f,Bound->TwistMax):M.Twist/FMath::Min(-1.f,Bound->TwistMin);
     C.Cost+=40*(FMath::Square(FMath::Max(0.f,R-.92f))+FMath::Square(FMath::Max(0.f,T-.92f)))+.5f*FMath::Square(FMath::Max(0.f,R-.7f));
    }
    const float Depth=HumanWallDepth(Goals,Result.World.TransformPosition(C.Middle));
    const float MidDepth=HumanWallDepth(Goals,Result.World.TransformPosition((C.Middle+C.Finish)/2));
    C.Cost+=100*FMath::Square(FMath::Max(0.f,(Arm?7.f:10.f)-Depth))+100*FMath::Square(FMath::Max(0.f,(Arm?4.f:6.f)-MidDepth));
    // the thigh's own middle counts as well, and so do the holds: a steered knee must not swing the leg through the
    // wall or through resin to get there (a leg may rest on the hold its own foot is on)
    if(!Arm)
    {
     const FVector Thigh=Result.World.TransformPosition((Start+C.Middle)/2),KneeAt=Result.World.TransformPosition(C.Middle),Shin=Result.World.TransformPosition((C.Middle+C.Finish)/2);
     C.Cost+=100*FMath::Square(FMath::Max(0.f,9.f-HumanWallDepth(Goals,Thigh)));
     for(const FHumanGoals::FDome& D:Goals.Domes)
     {
      if(D.Owners&(1<<Limb))continue;
      FVector Way;
      for(const TPair<FVector,float>& Ball:{TPair<FVector,float>(Thigh,8.f),TPair<FVector,float>(KneeAt,6.f),TPair<FVector,float>(Shin,5.f)})
       C.Cost+=100*FMath::Square(FMath::Max(0.f,1.5f-HumanDomeClear(D,Ball.Key,Ball.Value,Way)));
     }
    }
    return C;
   };
   const float AimHeight=FVector::DotProduct(Goals.Ends[Limb]-Goals.Roots[Limb],Goals.PanelUp[Limb]);
   const float SwivelLimit=!Arm&&!Goals.Held[Limb]&&Goals.Want[Limb]>.01f?FMath::Lerp(PI,1.05f,FMath::SmoothStep(-35.f,-5.f,AimHeight)):PI;
   float Psi=FMath::Clamp(Iteration==0?Swivels[Limb]:Chosen[Limb],-SwivelLimit,SwivelLimit);
   FCandidate Best=Attempt(Psi);
   if(Search && Iteration==0 && SteerKnees)
   {
    // The knee has one freedom left once the foot is placed: how far round the hip-to-foot line it sits. Clamping the
    // thigh after the fact, when the hip runs out of range, makes the leg jump about. Instead the knee is steered a
    // little each frame down the slope of the same cost the pose is judged by, which rises well before a joint
    // reaches its limit, so the thigh turns away from the edge of its range before it gets there.
    if(!Arm)
    {
     const float H=.05f;
     const FCandidate Less=Attempt(Psi-H),More=Attempt(Psi+H);
     const float Slope=(More.Cost-Less.Cost)/(2*H),Curve=(More.Cost-2*Best.Cost+Less.Cost)/(H*H);
     const float Step=-FMath::Clamp(Curve>1e-3f?Slope/Curve:Slope*.02f,-.05f,.05f);
     if(FMath::Abs(Step)>.004f){Psi=FMath::Clamp(Psi+Step,-FMath::Min(2.2f,SwivelLimit),FMath::Min(2.2f,SwivelLimit));Best=Attempt(Psi);}
    }
   }
   else if(Search && Iteration==0)
   {
    for(int32 I=0;I<36;I++)
    {
     const float A=-SwivelLimit+2*SwivelLimit*I/36;FCandidate C=Attempt(A);
     if(C.Cost<Best.Cost-.001f){Psi=A;Best=C;}
    }
    for(float H:{.12f,.05f,.02f})for(float Direction:{-1.f,1.f})
    {const float A=FMath::Clamp(Psi+H*Direction,-SwivelLimit,SwivelLimit);FCandidate C=Attempt(A);if(C.Cost<Best.Cost){Psi=A;Best=C;}}
   }
   Chosen[Limb]=Psi;Result.Local[U].SetRotation(Best.Upper);Result.Local[L].SetRotation(Best.Lower);UpdateBranch(U);
   // Share pronation/supination with the forearm instead of wringing the wrist.
   const FVector Forward=Arm?RestSpace[TipIndex].GetLocation()-RestSpace[E].GetLocation():Sole;
   const FVector Desired=Result.World.InverseTransformVectorNoScale(Goals.Along[Limb]);
   const FVector Facing=Result.World.InverseTransformVectorNoScale(Arm?-Goals.Out[Limb]:Goals.PanelUp[Limb]);
   const FQuat EndQ=FRig::Orient(RestSpace[E].GetRotation(),Forward,Arm?-RefUp:RefUp,Desired,Facing);
   if(Arm)
   {
    const FVector TwistAxis=RestSpace[L].GetRotation().Inverse().RotateVector(RestLower);
    const FQuat Relative=Space[L].GetRotation().Inverse()*EndQ;
    const float Projection=Relative.X*TwistAxis.X+Relative.Y*TwistAxis.Y+Relative.Z*TwistAxis.Z;
    const float Twist=FMath::UnwindRadians(2*FMath::Atan2(Projection,float(Relative.W)))*.5f;
    Put(L,Space[L].GetRotation()*FQuat(TwistAxis,Twist));
   }
   Put(E,EndQ);
   if(!PlantSole)break;
   const FVector ActualSole=Space[E].GetLocation()+Space[E].GetRotation().RotateVector(RestSpace[E].GetRotation().Inverse().RotateVector(Sole));
   const FVector Error=TargetSole-ActualSole;
   const float Miss=Error.Size();
   if(Miss<SmallestMiss)
   {
    SmallestMiss=Miss;
    BestLeg[0]=Result.Local[U].GetRotation();
    BestLeg[1]=Result.Local[L].GetRotation();
    BestLeg[2]=Result.Local[E].GetRotation();
   }
   if(Miss<.15f)break;
   Target+=Error.GetClampedToMaxSize(8.f)*.85;
  }
  if(PlantSole)
  {
   Result.Local[U].SetRotation(BestLeg[0]);
   Result.Local[L].SetRotation(BestLeg[1]);
   Result.Local[E].SetRotation(BestLeg[2]);
   UpdateBranch(U);
  }
  const bool HandContact=Arm && Goals.HandSurfaces[Limb-2].IsSet();
  if(PlantSole || HandContact)
  {
   const FVector ContactPoint=PlantSole?TargetSole:Result.World.InverseTransformPosition(Goals.HandSurfaces[Limb-2].GetValue()+Goals.Out[Limb]*2.5f);
   const FVector ContactOffset=PlantSole?Sole:RestSpace[TipIndex].GetLocation()-RestSpace[E].GetLocation();
   // The imported ankle frame and outsole offset differ across characters.
   // Refine the constrained joint rotations against the measured shoe vertex,
   // rather than repeatedly moving an ankle proxy with a fixed foot angle.
   const FVector SoleLocal=RestSpace[E].InverseTransformVector(ContactOffset);
   auto ContactCost=[&]()
   {
    const FVector Actual=Space[E].TransformPosition(SoleLocal);
    const float KneeDepth=HumanWallDepth(Goals,Result.World.TransformPosition(Space[L].GetLocation()));
    return FVector::DistSquared(Actual,ContactPoint)+100*FMath::Square(FMath::Max(0.f,(Arm?4.f:6.f)-KneeDepth));
   };
   float Cost=ContactCost();
   for(float Degrees:{3.f,1.f,.3f,.1f})for(int32 Sweep=0;Sweep<4 && Cost>.02f;++Sweep)
    for(int32 Joint:{U,L,E})for(const FVector& Axis:{FVector::ForwardVector,FVector::RightVector,FVector::UpVector})
    {
     const FQuat Before=Result.Local[Joint].GetRotation();FQuat BestQ=Before;
     for(float Sign:{-1.f,1.f})
     {
      const FQuat Q=LimitLocal(Joint,Before*FQuat(Axis,FMath::DegreesToRadians(Degrees*Sign)));
      Result.Local[Joint].SetRotation(Q);UpdateBranch(Joint);
      const float Next=ContactCost();if(Next<Cost){Cost=Next;BestQ=Q;}
     }
     Result.Local[Joint].SetRotation(BestQ);UpdateBranch(Joint);
    }
   SmallestMiss=FVector::Distance(Space[E].TransformPosition(SoleLocal),ContactPoint);
  }
  // A held arm supports the body economically near extension. Measure the
  // actual solved skeleton, not the 2D simulation's nominal shoulder. This
  // lets the pelvis search lower and away while all contacts remain fitted.
  if(Arm && Goals.Held[Limb] && Goals.HandSurfaces[Limb-2].IsSet()
     && Goals.Grips[Limb]!=TEXT("smear")
     && FVector::DotProduct(Goals.HandSurfaces[Limb-2].GetValue()-Goals.Roots[Limb],Goals.PanelUp[Limb])>0)
  {
   const float Span=FVector::Distance(Space[U].GetLocation(),Space[L].GetLocation())+FVector::Distance(Space[L].GetLocation(),Space[E].GetLocation());
   const float ArmExtension=FVector::Distance(Space[U].GetLocation(),Space[E].GetLocation());
   const float Fraction=(Goals.Held[0]||Goals.Held[1])?.92f:.97f;
   Result.SupportEffort+=.0125f*FMath::Square(FMath::Max(0.f,Span*Fraction-ArmExtension));
  }
  Result.Ends[Limb]=Result.World.TransformPosition(Space[E].GetLocation());
  Result.Bends[Limb]=Result.World.TransformPosition(Space[L].GetLocation());
  Result.Miss[Limb]=(PlantSole||HandContact)?SmallestMiss:FVector::Distance(Space[E].GetLocation(),Target);
  Result.MinClearance=FMath::Min(Result.MinClearance,HumanWallDepth(Goals,Result.Bends[Limb]));
 }
 const int32 Eyes=Bone(TEXT("PlayerEyes"));
 Result.Eye=Eyes>=0?Result.World.TransformPosition(Space[Eyes].GetLocation()):Result.World.TransformPosition(Space[Head].GetLocation())+Up*12;
 for(const auto& Pair:Limits)
 {
  const FHumanFrame& Frame=Frames[Pair.Key];const FQuat Relative=Neutral[Pair.Key].GetRotation().Inverse()*Result.Local[Pair.Key].GetRotation();
  const FHumanAngles M=Measure(Relative,Frame);
  Result.MaxLimitViolation=FMath::Max(Result.MaxLimitViolation,LimitsEnabled?Violation(Relative,Frame,Pair.Value):0.f);
  const float R=M.Swing/Pair.Value.Reach(M.Bearing), T=M.Twist>=0?M.Twist/FMath::Max(1.f,Pair.Value.TwistMax):M.Twist/FMath::Min(-1.f,Pair.Value.TwistMin);
  Result.Comfort+=FMath::Square(FMath::Max(0.f,(R-.7f)/.3f))+.6f*FMath::Square(FMath::Max(0.f,(T-.7f)/.3f));
 }
 for(const FTransform& T:Result.Local)Result.Finite&=!T.ContainsNaN();
 Result.Finite&=!Result.World.ContainsNaN();return Result;
}

void FHumanRig::PoseFingers(const FHumanGoals& Goals,FHumanPose& Pose,float Delta,uint8 NewlyHeld) const
{
 const bool BoundStep=Delta>0 && Previous.Num()==Pose.Local.Num();
 bool ApplyDamping=true;
 const float Dt=FMath::Min(Delta,.05f);
 const float MaxStep=JointRate*Dt, Follow=1-FMath::Exp(-11*Dt);
 auto SetRotation=[&](int32 BoneIndex,FQuat Target)
 {
  if(BoundStep && ApplyDamping)
  {
   const FQuat& Before=Previous[BoneIndex];
   const float Angle=Before.AngularDistance(Target);
   if(Angle>SMALL_NUMBER)Target=FQuat::Slerp(Before,Target,FMath::Min(Follow,MaxStep/Angle));
  }
  Pose.Local[BoneIndex].SetRotation(Target.GetNormalized());
 };
 // Sole landmarks are authored in the ankle frame. Keep the loaded toe in its
 // bind pose so toe-weighted rubber cannot lift away after the sole fit.
 // Articulated soles need a skinned contact landmark before enabling toe bend.
 for(int32 L=0;L<2;++L)
 {
  const int32 Toe=Bone(L==0?TEXT("RightToeBase"):TEXT("LeftToeBase"));
  if(Toe<0)continue;
  const FVector Axis=RestSpace[Toe].GetRotation().Inverse().RotateVector(RefSide);
  SetRotation(Toe,Rest[Toe].GetRotation()*FQuat(Axis,FMath::DegreesToRadians(Goals.Held[L]?0.f:-4.f)));
 }
 TArray<FTransform> Space;
 Compose(Pose.Local,Space);
 for(int32 Limb=2;Limb<4;++Limb)
 {
  const TCHAR* Side=Limb==2?TEXT("Right"):TEXT("Left");
  const FString& Grip=Goals.Grips[Limb];
  const FVector Over=Pose.World.InverseTransformVectorNoScale(Goals.Along[Limb]);
  const FVector In=Pose.World.InverseTransformVectorNoScale(-Goals.Out[Limb]);
  // the hold this hand is on, as resin: each finger closes until its tip meets it
  const FHumanGoals::FDome* Resin=nullptr;
  for(const FHumanGoals::FDome& D:Goals.Domes)if(D.Owners&(1<<Limb)){Resin=&D;break;}
  // every finger closes onto the resin by the same rule and then the hand is evened out: no finger curls much past the
  // middle of the four, so the hand reads as one grip instead of four different ones
  const TCHAR* FingerNames[4]={TEXT("Index"),TEXT("Middle"),TEXT("Ring"),TEXT("Pinky")};
  int32 Chains[4][3];bool Has[4];float Curls[4]={1,1,1,1};
  auto Bend=[&](int32 J)
  {
   if(Grip==TEXT("jug")) return J==0?16.f:J==1?58.f:76.f;
   if(Grip==TEXT("crimp")) return J==0?12.f:J==1?80.f:55.f;
   if(Grip==TEXT("pinch")) return J==0?20.f:J==1?45.f:64.f;
   if(Grip==TEXT("sloper")||Grip==TEXT("smear")) return 4.f*J;
   return 8.f*(J+1);
  };
  const float Arch=Grip==TEXT("crimp")?12.f:Grip==TEXT("jug")?10.f:Grip==TEXT("pinch")||Grip==TEXT("pocket")?10.f:0.f;
  auto DigitTip=[&](const int32* Chain)
  {
   const FVector RestLast=RestSpace[Chain[2]].GetLocation()-RestSpace[Chain[1]].GetLocation();
   const FVector LocalLast=RestSpace[Chain[2]].GetRotation().UnrotateVector(RestLast);
   const FVector Last=Space[Chain[2]].GetRotation().RotateVector(LocalLast);
   return Pose.World.TransformPosition(Space[Chain[2]].GetLocation()+Last*.8f);
  };
  // poses one finger at Scale of its grip's curl and returns where the fingertip ends up, in gym space
  auto Curl=[&](int32 F,float Scale)
  {
   const int32* Chain=Chains[F];
   for(int32 J=0;J<3;++J)
   {
    const int32 I=Chain[J],Parent=Parents[I];
    const FVector RestAxis=J<2?RestSpace[Chain[J+1]].GetLocation()-RestSpace[I].GetLocation()
     :RestSpace[I].GetLocation()-RestSpace[Parent].GetLocation();
    // sloper and open hands still bend a little at each knuckle so they can drape over the resin
    // a gripping hand is arched: the first bone lifts off the hold and the two after it curl back down onto it,
    // so the fingers read as pulling instead of lying flat
    const float Angle=FMath::DegreesToRadians(FMath::Max(Bend(J),Resin?20.f+12.f*J:0.f)*Scale*(J==0?1.f:1+Arch/40)-(J==0&&Resin?Arch:0.f));
    const FVector Aim=(Over*FMath::Cos(Angle)+In*FMath::Sin(Angle)).GetSafeNormal();
    const FQuat Carried=(Space[Parent].GetRotation()*Rest[I].GetRotation()).GetNormalized();
    const FVector Axis=Carried.RotateVector(RestSpace[I].GetRotation().Inverse().RotateVector(RestAxis)).GetSafeNormal();
    const FQuat Turn=FQuat::FindBetweenNormals(Axis,Aim);
    const float MaxTurn=FMath::DegreesToRadians(J==0?48.f:J==1?100.f:85.f);
    const FQuat Rotation=FQuat::Slerp(FQuat::Identity,Turn,FMath::Min(1.f,MaxTurn/FMath::Max(float(Turn.GetAngle()),.001f)))*Carried;
    SetRotation(I,Space[Parent].GetRotation().Inverse()*Rotation);
    Space[I]=Pose.Local[I]*Space[Parent];
   }
   return DigitTip(Chain);
  };
  // Measure the geometric wrap first. Damping every bisection candidate makes
  // the fitted target depend on last frame and can make it alternate.
  ApplyDamping=false;
  for(int32 F=0;F<4;++F)
  {
   Has[F]=Grip!=TEXT("free");
   for(int32 J=0;J<3;++J){Chains[F][J]=Bone(*FString::Printf(TEXT("%sHand%s%d"),Side,FingerNames[F],J+1));Has[F]&=Chains[F][J]>=0;}
   if(!Has[F]||!Resin)continue;
   // close from open until the tip touches the resin (or the wall), never past the grip's full curl: a
   // bisection on one curl amount, so the wrap changes smoothly as the hand moves on the hold
   auto Touching=[&](float Scale)
   {
    // the fingertip and the middle knuckle both stop on the real resin, not a rough dome
    const FVector P=Curl(F,Scale);FVector Way;
    const FVector Knuckle=Pose.World.TransformPosition(Space[Chains[F][1]].GetLocation());
    const float Skin=.9f;
    const bool Hit=Resin->Surface.IsValid()?FMath::Min(Resin->Surface->Clear(P),Resin->Surface->Clear(Knuckle))<Skin:HumanDomeClear(*Resin,P,Skin,Way)<=0;
    return Hit || HumanWallDepth(Goals,P)<1.f;
   };
   if(Grip==TEXT("crimp"))
   {
    // The approach can intersect the lip before the finger reaches its final
    // hooked pose. Search for a clear final pose, preferring a loaded tip.
    float Best=MAX_flt;
    for(int32 Sample=0;Sample<=32;++Sample)
    {
     const float Scale=FMath::Lerp(-.15f,1.25f,Sample/32.f);
     if(Touching(Scale)) continue;
     const FVector Tip=DigitTip(Chains[F]);
     FVector Way;
     const float Clear=Resin->Surface.IsValid()?Resin->Surface->Clear(Tip):HumanDomeClear(*Resin,Tip,0,Way);
     const float Cost=FMath::Abs(Clear-1.f)+1.25f*FMath::Abs(Scale-1.f);
     if(Cost<Best){Best=Cost;Curls[F]=Scale;}
    }
    if(Best<MAX_flt) continue;
   }
   // a hand pressed right onto the resin lets the fingers lie a little flatter than straight
   // Sweep to the FIRST collision. A curled fingertip can enter and then
   // leave a rounded hold; checking only the final curl misses that crossing.
   float Low=-.15f;
   if(!Touching(Low))for(int32 Sample=1;Sample<=28;++Sample)
   {
    float High=FMath::Lerp(-.15f,1.25f,Sample/28.f);
    if(Touching(High))
    {
     for(int32 Step=0;Step<10;++Step){const float Mid=(Low+High)*.5f;if(Touching(Mid))High=Mid;else Low=Mid;}
     break;
    }
    Low=High;
   }
   Curls[F]=Low;
  }
  if(Resin)
  {
   TArray<float,TInlineAllocator<4>> Sorted;
   for(int32 F=0;F<4;++F)if(Has[F])Sorted.Add(Curls[F]);
   Sorted.Sort();
   if(Sorted.Num()>=2)
   {
    // opening a finger lifts it off the resin a touch; closing one further would push it through
    const float Middle=(Sorted[(Sorted.Num()-1)/2]+Sorted[Sorted.Num()/2])*.5f;
    for(int32 F=0;F<4;++F)Curls[F]=FMath::Min(Curls[F],Middle+.10f);
   }
  }
  ApplyDamping=(NewlyHeld & (1<<Limb))==0;
  for(int32 F=0;F<4;++F)if(Has[F])Curl(F,Curls[F]);

  // A thumb folds from its own palm-relative rest pose. A wall-space target
  // can swing the tip across the index finger as the wrist turns on an edge.
  float GripCurl=0;
  for(int32 F=0;F<4;++F) if(Has[F]) GripCurl+=FMath::Clamp(Curls[F],0.f,1.f)*.25f;
  int32 Thumb[3];
  for(int32 J=0;J<3;++J) Thumb[J]=Bone(*FString::Printf(TEXT("%sHandThumb%d"),Side,J+1));
  if(Grip==TEXT("free") || Thumb[0]<0 || Thumb[1]<0 || Thumb[2]<0) continue;
  auto Fold=[&](float Amount)
  {
   for(int32 J=0;J<3;++J)
   {
    const int32 I=Thumb[J],Parent=Parents[I];
    const FQuat Carried=Space[Parent].GetRotation()*Rest[I].GetRotation();
    const FVector Axis=Carried.UnrotateVector(Over^In).GetSafeNormal();
    SetRotation(I,Rest[I].GetRotation()*FQuat(Axis,FMath::DegreesToRadians((J==0?12.f:16.f)*Amount)));
    Space[I]=Pose.Local[I]*Space[Parent];
   }
   return DigitTip(Thumb);
  };
  const float Natural=FMath::Lerp(.45f,1.f,GripCurl);
  if(!Resin){Fold(Natural);continue;}
  const bool SmoothThumb=ApplyDamping;
  ApplyDamping=false;
  float BestAmount=Natural,BestClearance=-MAX_flt;
  const auto Clearance=[&](const FVector& Point)
  {
   FVector Way;
   return FMath::Min(HumanWallDepth(Goals,Point),
    Resin->Surface.IsValid()?Resin->Surface->Clear(Point):HumanDomeClear(*Resin,Point,0,Way));
  };
  // Try the natural curl first, then open the thumb only as much as needed.
  for(int32 Sample=0;Sample<=12;++Sample)
  {
   const float Amount=FMath::Lerp(Natural,-1.8f,Sample/12.f);
   const FVector Tip=Fold(Amount);
   const FVector Base=Pose.World.TransformPosition(Space[Thumb[1]].GetLocation());
   const FVector Joint=Pose.World.TransformPosition(Space[Thumb[2]].GetLocation());
   const float Clear=FMath::Min3(Clearance((Base+Joint)*.5),Clearance((Joint+Tip)*.5),Clearance(Tip));
   if(Clear>BestClearance){BestClearance=Clear;BestAmount=Amount;}
   if(Clear>=.7f)break;
  }
  ApplyDamping=SmoothThumb;
  Fold(BestAmount);
 }
}

// Fit the body to smoothed goals, then steer the knees and check clearance
// before posing the contacts for the rendered frame.
FHumanPose FHumanRig::Solve(const FHumanGoals& InputGoals,float Delta)
{
 if(UseLegacy)return SolveLegacy(InputGoals,Delta);
 if(!Ready())return {};
 const FVector Pelvis=(InputGoals.Roots[0]+InputGoals.Roots[1])/2;
 if(HasHistory && FVector::Dist(Pelvis,LastPelvis)>100)Reset();
 uint8 NewlyHeld=0;
 if(HasHistory)
  for(int32 Limb=2;Limb<4;++Limb)
   if(InputGoals.Held[Limb] && !SmoothGoals.Held[Limb]) NewlyHeld|=1<<Limb;
 if(!HasHistory || Delta<=0 || Delta>=.25f)SmoothGoals=InputGoals;
 else
 {
  const float Dt=FMath::Min(Delta,.05f);
  const float Body=1-FMath::Exp(-10*Dt), Reach=1-FMath::Exp(-14*Dt), Turn=1-FMath::Exp(-5*Dt);
  FHumanGoals Next=InputGoals;
  for(int32 L=0;L<4;++L)
  {
   Next.Roots[L]=FMath::Lerp(SmoothGoals.Roots[L],InputGoals.Roots[L],Body);
   if(!InputGoals.Held[L]) Next.Ends[L]=FMath::Lerp(SmoothGoals.Ends[L],InputGoals.Ends[L],Reach);
   // knees and elbows swing over slowly, so a high step never snaps into a drop knee
   // a knee swings over more slowly still: the thigh is the heaviest thing a climber moves
   Next.Poles[L]=FMath::Lerp(SmoothGoals.Poles[L],InputGoals.Poles[L],L<2?1-FMath::Exp(-3*Dt):Turn);
   if(!InputGoals.Held[L]) Next.Along[L]=FMath::Lerp(SmoothGoals.Along[L],InputGoals.Along[L],Body).GetSafeNormal();
   Next.Out[L]=FMath::Lerp(SmoothGoals.Out[L],InputGoals.Out[L],Body).GetSafeNormal();
   Next.Want[L]=FMath::Lerp(SmoothGoals.Want[L],InputGoals.Want[L],Body);


  }
  Next.Look=FMath::Lerp(SmoothGoals.Look,InputGoals.Look,Turn).GetSafeNormal();
  SmoothGoals=MoveTemp(Next);
 }
 float Ignored[4];
 // Fit body placement to contacts and support effort before steering limbs.
 const bool Settle=HasHistory && Delta>0 && Delta<.25f;
 FVector Wanted=Shift;
 // Newton steps on the contact cost along each body axis: a smooth function of
 // the goals, unlike a pattern search whose answer jumps between grid points.
 const FVector Axes[3]={SmoothGoals.Out[0],(SmoothGoals.Roots[3]-SmoothGoals.Roots[2]).GetSafeNormal(),(SmoothGoals.Roots[2]+SmoothGoals.Roots[3]-SmoothGoals.Roots[0]-SmoothGoals.Roots[1]).GetSafeNormal()};
 // The pelvis may also tip and turn a little under the spine (Corey's hip
 // opening): it swings knees off the wall and frees a high step. Same Newton
 // steps, with a cost on the angle itself so it only opens when it helps.
 float WantPitch=Pitch,WantYaw=Yaw;
 auto Score=[&](FVector At,float P,float Y){return Pass(SmoothGoals,At,Swivel,false,Ignored,P,Y,true).Cost(SmoothGoals)+40*(P*P+Y*Y);};
 const float H=1.5f,HA=.03f;
 auto Newton=[](float Plus,float Here,float Minus,float Step,float Most){const float Slope=(Plus-Minus)/(2*Step),Curve=(Plus-2*Here+Minus)/(Step*Step);return -FMath::Clamp(Curve>1e-3f?Slope/Curve:Slope*.05f,-Most,Most);};
 for(int32 Iteration=0;Iteration<(Settle?2:ColdIterations);++Iteration)
 {
  const float Here=Score(Wanted,WantPitch,WantYaw);FVector Step=FVector::ZeroVector;
  for(const FVector& Axis:Axes)
  {
   if(Axis.IsNearlyZero())continue;
   Step+=Axis*Newton(Score(Wanted+Axis*H,WantPitch,WantYaw),Here,Score(Wanted-Axis*H,WantPitch,WantYaw),H,4.f);
  }
  const float DP=Newton(Score(Wanted,WantPitch+HA,WantYaw),Here,Score(Wanted,WantPitch-HA,WantYaw),HA,.08f);
  const float DY=Newton(Score(Wanted,WantPitch,WantYaw+HA),Here,Score(Wanted,WantPitch,WantYaw-HA),HA,.08f);
  WantPitch=FMath::Clamp(WantPitch+DP,-.31f,.31f);WantYaw=FMath::Clamp(WantYaw+DY,-.45f,.45f);
  Wanted=(Wanted+Step).GetClampedToMaxSize(MaxShift);
  const float Depth=HumanWallDepth(SmoothGoals,Pelvis+Wanted);
  // keep the pelvis off the wall, but inside its bound
  if(Depth<18)
  {
   // the way out from the wall comes first; what is left of the bound is shared by the sideways and up-down parts
   Wanted+=SmoothGoals.Out[0]*(18-Depth);
   const float Off=FMath::Clamp(float(FVector::DotProduct(Wanted,SmoothGoals.Out[0])),-MaxShift,MaxShift);
   const FVector Along=(Wanted-SmoothGoals.Out[0]*FVector::DotProduct(Wanted,SmoothGoals.Out[0])).GetClampedToMaxSize(FMath::Sqrt(FMath::Max(0.f,MaxShift*MaxShift-Off*Off)));
   Wanted=SmoothGoals.Out[0]*Off+Along;
  }
 }
 // The search can always find a placement a hair better than the last, and chasing those is what makes a still
 // climber tremble. The body only moves as far as the new placement is really better than where it already is.
 if(Settle)
 {
  const float Now=Score(Shift,Pitch,Yaw),Then=Score(Wanted,WantPitch,WantYaw);
  const float Better=FMath::SmoothStep(.015f,.3f,(Now-Then)/FMath::Max(Now,.5f));
  Wanted=FMath::Lerp(Shift,Wanted,Better);WantPitch=FMath::Lerp(Pitch,WantPitch,Better);WantYaw=FMath::Lerp(Yaw,WantYaw,Better);
 }
 const float Ease=1-FMath::Exp(-8*FMath::Min(Delta,.05f));
 Shift=Settle?FMath::Lerp(Shift,Wanted,Ease):Wanted;
 // the pelvis tips and turns no faster than a hip can follow: a quick tilt throws the thighs against their limits
 const float Tilt=.45f*FMath::Min(Delta,.05f);
 Pitch=Settle?Pitch+FMath::Clamp((WantPitch-Pitch)*Ease*.6f,-Tilt,Tilt):WantPitch;
 Yaw=Settle?Yaw+FMath::Clamp((WantYaw-Yaw)*Ease*.6f,-Tilt,Tilt):WantYaw;
 // the last pass steers each knee; a pose with no history gets several rounds to find where its knees belong
 float Turned[4]={};
 SteerKnees=true;
 FHumanPose Best;
 for(int32 Round=0;Round<(Settle?1:12);++Round)
 {
  Best=Pass(SmoothGoals,Shift+Push,Swivel,true,Turned,Pitch,Yaw,true);
  Swivel[0]=Turned[0];Swivel[1]=Turned[1];
 }
 SteerKnees=false;
 // Corey's first rule: no part of the body is in the wall. Sample the thighs,
 // shins, pelvis, trunk and head as balls; whatever is deepest moves the whole
 // body out along that panel's face. The push comes on fast and lets go slowly.
 {
  TArray<FTransform> Space;Compose(Best.Local,Space);
  auto At=[&](const TCHAR* Name){const int32 I=Bone(Name);return I>=0?Best.World.TransformPosition(Space[I].GetLocation()):FVector(NAN);};
  struct FBall{FVector P;float R;int32 Limb;};TArray<FBall,TInlineAllocator<24>> Balls;
  int32 Owner=-1;
  auto Along=[&](FVector A,FVector B,float R){if(A.ContainsNaN()||B.ContainsNaN())return;for(float T:{.3f,.5f,.7f})Balls.Add({FMath::Lerp(A,B,T),R,Owner});};
  for(const TCHAR* Side:{TEXT("Left"),TEXT("Right")})
  {
   const FString P(Side);Owner=P==TEXT("Right")?0:1;
   Along(At(*(P+TEXT("UpLeg"))),At(*(P+TEXT("Leg"))),8);
   Along(At(*(P+TEXT("Leg"))),At(*(P+TEXT("Foot"))),5);
   const FVector Knee=At(*(P+TEXT("Leg")));if(!Knee.ContainsNaN())Balls.Add({Knee,6,Owner});
  }
  Owner=-1;
  const FVector Hips=At(TEXT("Hips")),Head=At(TEXT("Head"));
  if(!Hips.ContainsNaN())Balls.Add({Hips,12,-1});
  Along(Hips,Head,13);
  if(!Head.ContainsNaN())Balls.Add({Head,10,-1});
  float Worst=0;FVector Out=SmoothGoals.Out[0];
  for(const FBall& B:Balls)
  {
   FVector N;float Deep=B.R+1-HumanWallDepth(SmoothGoals,B.P,&N);if(Deep>Worst){Worst=Deep;Out=N;}
   // a shin may rest on the hold its own foot is on
   for(const FHumanGoals::FDome& D:SmoothGoals.Domes)
   {
    if(B.Limb>=0 && (D.Owners&(1<<B.Limb)))continue;
    FVector Way;Deep=-HumanDomeClear(D,B.P,B.R,Way)+.5f;if(Deep>Worst){Worst=Deep;Out=Way;}
   }
  }
  const FVector Clear=Worst>0?Push+Out*Worst:FVector::ZeroVector;
  const float Dt=FMath::Min(Delta,.05f);
  const FVector Next=!Settle?Clear:FMath::Lerp(Push,Clear,1-FMath::Exp(-(Worst>0?20.f:3.f)*Dt));
  if(!Next.Equals(Push,.05f))
  {
   Push=Next.GetClampedToMaxSize(50);
   Best=Pass(SmoothGoals,Shift+Push,Swivel,false,Ignored,Pitch,Yaw,true);
  }
 }
 // A speed ceiling on each joint for sudden goal changes (a new grip, a knee
 // flipping sides). The inputs are already smooth, so it rarely engages.
 if(Settle && Previous.Num()==Best.Local.Num())
 {
  const float Dt=FMath::Min(Delta,.05f);
  for(int32 I=0;I<Best.Local.Num();I++)
  {
   const FQuat Target=Best.Local[I].GetRotation();const float Angle=Previous[I].AngularDistance(Target);
   const float Rate=(SlowJoints[I]?JointRate*.5f:JointRate)*Dt;
   // the contact fit inside Pass is a greedy search and can land a little
   // differently each frame; a light low-pass filters that out
   float Follow=1-FMath::Exp(-(SlowJoints[I]?7.f:11.f)*Dt);
   if(Angle*Follow>Rate)Follow=Rate/Angle;
   if(Angle>SMALL_NUMBER)Best.Local[I].SetRotation(LimitLocal(I,FQuat::Slerp(Previous[I],Target,Follow)));
  }
 }
 PoseFingers(SmoothGoals,Best,Settle?Delta:0.f,NewlyHeld);
 // Report the rendered pose, not the unsmoothed fit that preceded it.
 TArray<FTransform> Rendered;Compose(Best.Local,Rendered);
 const TCHAR* EndNames[]={TEXT("RightFoot"),TEXT("LeftFoot"),TEXT("RightHand"),TEXT("LeftHand")};
 for(int32 L=0;L<4;++L)
 {
  const int32 E=Bone(EndNames[L]);
  FVector Actual=Best.World.TransformPosition(Rendered[E].GetLocation()),Target=SmoothGoals.Ends[L];
  if(L<2 && SmoothGoals.ShoeSurfaces[L].IsSet())
  {
   const int32 Contact=Bone(L==0?TEXT("RightSoleContact"):TEXT("LeftSoleContact"));
   const FVector Offset=Contact>=0?RestSpace[Contact].GetLocation()-RestSpace[E].GetLocation():RefFront*19-RefUp*4.7;
   Actual=Best.World.TransformPosition(Rendered[E].TransformPosition(RestSpace[E].InverseTransformVector(Offset)));
   Target=SmoothGoals.ShoeSurfaces[L].GetValue();
  }
  else if(L>=2 && SmoothGoals.HandSurfaces[L-2].IsSet())
  {
   const int32 Contact=Bone(L==2?TEXT("RightHandMiddle1"):TEXT("LeftHandMiddle1"));
   Actual=Best.World.TransformPosition(Rendered[Contact].GetLocation());
   Target=SmoothGoals.HandSurfaces[L-2].GetValue()+SmoothGoals.Out[L]*2.5f;
  }
  Best.Miss[L]=FVector::Distance(Actual,Target);
 }
 PreviousWorld=Best.World;
 Previous.SetNum(Best.Local.Num());for(int32 I=0;I<Best.Local.Num();I++)Previous[I]=Best.Local[I].GetRotation();
 HasHistory=Delta>0;LastPelvis=Pelvis;
 return Best;
}

FHumanPose FHumanRig::SolveLegacy(const FHumanGoals& InputGoals,float Delta)
{
 if(!Ready())return {};
 FHumanGoals Goals=InputGoals;
 const FVector Pelvis=(Goals.Roots[0]+Goals.Roots[1])/2;
 if(HasHistory && FVector::Dist(Pelvis,LastPelvis)>100)Reset();
 const bool Smooth=HasHistory&&Delta>0&&Delta<.25;
 // The pole itself moves with foot height. Smooth that frame as well as the
 // swivel angle, otherwise a high step can abruptly turn into a drop knee.
 for(int32 L=0;L<4;++L)
 {
  if(Smooth)Goals.Poles[L]=FMath::Lerp(PreviousPoles[L],Goals.Poles[L],1-FMath::Exp(-4*FMath::Min(Delta,.05f)));
  PreviousPoles[L]=Goals.Poles[L];
 }
 float Candidate[4]={};
 FHumanPose Best=Pass(Goals,Shift,Swivel,true,Candidate,Pitch,Yaw);
 float BestCost=Best.Cost(Goals);FVector Wanted=Shift;float WantedPitch=Pitch,WantedYaw=Yaw;
 const FVector Up=(Goals.Roots[2]+Goals.Roots[3]-Goals.Roots[0]-Goals.Roots[1]).GetSafeNormal();
 const FVector Side=(Goals.Roots[3]-Goals.Roots[2]).GetSafeNormal();
 const FVector Out=Goals.Out[0];
 for(float Step:{8.f,4.f,2.f})
 {
  FVector Gap=FVector::ZeroVector;float Weights=0;
  for(int32 L=0;L<4;L++)if(Goals.Held[L]){const float W=L>=2?1.4f:1.f;Gap+=(Goals.Ends[L]-Best.Ends[L])*W;Weights+=W;}
  TArray<FVector,TInlineAllocator<7>> Offsets={Up*Step,-Up*Step,Side*Step,-Side*Step,Out*Step,-Out*Step};
  if(Weights>0)Offsets.Add(Gap/Weights*.5f);
  const FVector Center=Wanted;
  for(const FVector& D:Offsets)
  {
   FVector Next=(Center+D).GetClampedToMaxSize(MaxShift);
   if(HumanWallDepth(Goals,Pelvis+Next)<18)continue;
   float TryAngles[4];FHumanPose Trial=Pass(Goals,Next,Candidate,true,TryAngles,WantedPitch,WantedYaw);
   if(Trial.Cost(Goals)>=BestCost-.001f)continue;
   Best=MoveTemp(Trial);BestCost=Best.Cost(Goals);Wanted=Next;for(int32 L=0;L<4;L++)Candidate[L]=TryAngles[L];
  }
 }

 // A high step also tilts the pelvis. Share the counter-bend through the
 // constrained spine instead of twisting either hip beyond its range.
 for(float Step:{.21f,.105f,.0525f})
 {
  const float Center=WantedPitch;
  for(float D:{-Step,Step})
  {
   const float Next=FMath::Clamp(Center+D,-.31f,.31f);float Angles[4];
   FHumanPose Trial=Pass(Goals,Wanted,Candidate,true,Angles,Next,WantedYaw);
   if(Trial.Cost(Goals)+FMath::Square(Next)*3>=BestCost)continue;
   Best=MoveTemp(Trial);BestCost=Best.Cost(Goals);WantedPitch=Next;for(int32 L=0;L<4;L++)Candidate[L]=Angles[L];
  }
 }

 for(float Step:{.24f,.12f,.06f})
 {
  const float Center=WantedYaw;
  for(float D:{-Step,Step})
  {
   const float Next=FMath::Clamp(Center+D,-.45f,.45f);float Angles[4];
   FHumanPose Trial=Pass(Goals,Wanted,Candidate,true,Angles,WantedPitch,Next);
   if(Trial.Cost(Goals)+FMath::Square(Next)*3>=BestCost)continue;
   Best=MoveTemp(Trial);BestCost=Best.Cost(Goals);WantedYaw=Next;for(int32 L=0;L<4;L++)Candidate[L]=Angles[L];
  }
 }

 // Recheck body placement with this mesh's actual outsole geometry. Only
 // refine when a held contact still misses; never stretch bones or relax ROM.
 float FitAngles[4];FHumanPose Fitted=Pass(Goals,Wanted,Candidate,false,FitAngles,WantedPitch,WantedYaw,true);
 bool Refine=false;for(int32 L=0;L<4;++L)Refine|=Goals.Held[L] && Fitted.Miss[L]>1.f;
 if(Refine)
 {
  float FitCost=Fitted.Cost(Goals);
  for(float Step:{6.f,3.f,1.f})
  {
   const FVector Center=Wanted;
   for(const FVector& D:{Up*Step,-Up*Step,Side*Step,-Side*Step,Out*Step,-Out*Step})
   {
    const FVector Next=(Center+D).GetClampedToMaxSize(MaxShift);
    if(HumanWallDepth(Goals,Pelvis+Next)<18)continue;
    float Angles[4];FHumanPose Trial=Pass(Goals,Next,Candidate,true,Angles,WantedPitch,WantedYaw,true);
    const float Cost=Trial.Cost(Goals);
    if(Cost>=FitCost-.001f)continue;
    FitCost=Cost;Wanted=Next;for(int32 L=0;L<4;++L)Candidate[L]=Angles[L];
   }
  }
 }
 if(Smooth)
 {
  const float Dt=FMath::Min(Delta,.05f);
  Shift=FMath::Lerp(Shift,Wanted,1-FMath::Exp(-12*Dt));
  Pitch=FMath::Lerp(Pitch,WantedPitch,1-FMath::Exp(-8*Dt));
  Yaw=FMath::Lerp(Yaw,WantedYaw,1-FMath::Exp(-8*Dt));
  for(int32 L=0;L<4;L++)Candidate[L]=Swivel[L]+FMath::Clamp(FMath::FindDeltaAngleRadians(Swivel[L],Candidate[L]),-SwivelRate*Dt,SwivelRate*Dt);
  float Ignored[4];Best=Pass(Goals,Shift,Candidate,false,Ignored,Pitch,Yaw,true);
  PoseFingers(Goals,Best);
  for(int32 I=0;I<Best.Local.Num();I++)
  {
   const FQuat Target=Best.Local[I].GetRotation();FQuat Q=Target;const float Angle=Previous[I].AngularDistance(Q);
   const float Rate=SlowJoints[I]?JointRate*.5f:JointRate;
   // Ease toward the target like a damped spring, with the rate as a ceiling.
   // A pure rate cap moves at full speed until it overshoots, which buzzes.
   const float Follow=Angle>SMALL_NUMBER?FMath::Min(1-FMath::Exp(-(SlowJoints[I]?9.f:14.f)*Dt),Rate*Dt/Angle):1.f;
   Q=FQuat::Slerp(Previous[I],Q,Follow);
   Q=LimitLocal(I,Q);
   // Projection onto an asymmetric joint cone can move a rate-limited slerp
   // farther than its allowed step. Search a shorter valid step; never snap
   // through the cone boundary to enforce one constraint at the other's cost.
   if(Previous[I].AngularDistance(Q)>Rate*Dt+.00001f)
   {
    float Low=0,High=Angle>SMALL_NUMBER?FMath::Min(1.f,Rate*Dt/Angle):1.f;
    Q=Previous[I];
    for(int32 Iteration=0;Iteration<12;Iteration++)
    {
     const float Alpha=(Low+High)*.5f;
     const FQuat Trial=LimitLocal(I,FQuat::Slerp(Previous[I],Target,Alpha));
     if(Previous[I].AngularDistance(Trial)<=Rate*Dt){Q=Trial;Low=Alpha;}else High=Alpha;
    }
   }
   Best.Local[I].SetRotation(Q);
  }
  // Refine the bounded animated step itself. Otherwise filtering a fitted
  // endpoint can accumulate contact error or cut a knee through the wall.
  const TCHAR* AnimatedUpper[]={TEXT("RightUpLeg"),TEXT("LeftUpLeg"),TEXT("RightArm"),TEXT("LeftArm")};
  const TCHAR* AnimatedLower[]={TEXT("RightLeg"),TEXT("LeftLeg"),TEXT("RightForeArm"),TEXT("LeftForeArm")};
  const TCHAR* AnimatedEnd[]={TEXT("RightFoot"),TEXT("LeftFoot"),TEXT("RightHand"),TEXT("LeftHand")};
  for(int32 L=0;L<4;++L)
  {
   const bool Arm=L>=2;const int32 U=Bone(AnimatedUpper[L]),Mid=Bone(AnimatedLower[L]),E=Bone(AnimatedEnd[L]);
   const int32 Landmark=Arm?Bone(L==2?TEXT("RightHandMiddle1"):TEXT("LeftHandMiddle1")):Bone(L==0?TEXT("RightSoleContact"):TEXT("LeftSoleContact"));
   if(Landmark<0)continue;
   const FVector LocalPoint=RestSpace[E].InverseTransformPosition(RestSpace[Landmark].GetLocation());
   const FVector TargetPoint=Arm?Goals.HandSurfaces[L-2].Get(Goals.Ends[L])+Goals.Out[L]*2.5f:Goals.ShoeSurfaces[L].Get(Goals.Ends[L]);
   auto Cost=[&]()
   {
    TArray<FTransform> P;Compose(Best.Local,P);
    const float Depth=HumanWallDepth(Goals,Best.World.TransformPosition(P[Mid].GetLocation()));
    const FVector Contact=Best.World.TransformPosition(P[E].TransformPosition(LocalPoint));
    return FVector::DistSquared(Contact,TargetPoint)*(Goals.Held[L]?100.f:1.f)+1000*FMath::Square(FMath::Max(0.f,2.f-Depth));
   };
   float Score=Cost();
   for(int32 Sweep=0;Sweep<6;++Sweep)for(int32 I:{U,Mid,E})for(const FVector& Axis:{FVector::ForwardVector,FVector::RightVector,FVector::UpVector})
   {
    const FQuat Before=Best.Local[I].GetRotation();FQuat Chosen=Before;
    for(float Sign:{-1.f,1.f})
    {
     const FQuat Q=LimitLocal(I,Before*FQuat(Axis,Sign*.004f));
     if(Previous[I].AngularDistance(Q)>JointRate*Dt)continue;
     // Require a real gain; chasing tiny improvements flips direction every frame.
     Best.Local[I].SetRotation(Q);const float Next=Cost();if(Next<Score*.98f-.05f){Score=Next;Chosen=Q;}
    }
    Best.Local[I].SetRotation(Chosen);
   }
  }
  // A legal endpoint pose can still take a slerp through the wall. Advance
  // only along a collision-free portion of this frame's bounded joint step.
  const TArray<FTransform> Intended=Best.Local;const FTransform IntendedWorld=Best.World;
  const TCHAR* ClearanceBones[]={TEXT("RightLeg"),TEXT("LeftLeg"),TEXT("RightForeArm"),TEXT("LeftForeArm")};
  for(float Alpha:{1.f,.5f,.25f,.125f,.0625f,0.f})
  {
   Best.Local=Intended;
   Best.World.Blend(PreviousWorld,IntendedWorld,Alpha);
   bool Legal=true;
   for(int32 I=0;I<Best.Local.Num();++I)
   {
    const FQuat Q=FQuat::Slerp(Previous[I],Intended[I].GetRotation(),Alpha).GetNormalized();
    Best.Local[I].SetRotation(Q);
    const auto* Bound=Limits.Find(I);const auto* Frame=Frames.Find(I);
    if(Bound&&Frame)Legal &= Violation(Neutral[I].GetRotation().Inverse()*Q,*Frame,*Bound)<.03f;
   }
   TArray<FTransform> Check;Compose(Best.Local,Check);
   for(const TCHAR* Name:ClearanceBones)Legal &= HumanWallDepth(Goals,Best.World.TransformPosition(Check[Bone(Name)].GetLocation()))>=.5f;
   if(Legal)break;
  }
  TArray<FTransform> Space;Compose(Best.Local,Space);
  const TCHAR* End[]={TEXT("RightFoot"),TEXT("LeftFoot"),TEXT("RightHand"),TEXT("LeftHand")};
  const TCHAR* Middle[]={TEXT("RightLeg"),TEXT("LeftLeg"),TEXT("RightForeArm"),TEXT("LeftForeArm")};
  Best.MinClearance=MAX_flt;Best.MaxLimitViolation=0;
  for(int32 L=0;L<4;L++)
  {
   Best.Ends[L]=Best.World.TransformPosition(Space[Bone(End[L])].GetLocation());Best.Bends[L]=Best.World.TransformPosition(Space[Bone(Middle[L])].GetLocation());
   Best.Miss[L]=FVector::Distance(Best.Ends[L],Goals.Ends[L]);
   if(L<2 && Goals.ShoeSurfaces[L].IsSet())
   {
    const int32 Foot=Bone(End[L]);
    const int32 Contact=Bone(L==0?TEXT("RightSoleContact"):TEXT("LeftSoleContact"));
    const FVector Offset=Contact>=0?RestSpace[Contact].GetLocation()-RestSpace[Foot].GetLocation():RefFront*19-RefUp*4.7;
    const FVector Sole=RestSpace[Foot].GetRotation().Inverse().RotateVector(Offset);
    Best.Miss[L]=FVector::Distance(Goals.ShoeSurfaces[L].GetValue(),
     Best.World.TransformPosition(Space[Foot].GetLocation()+Space[Foot].GetRotation().RotateVector(Sole)));
   }
   Best.MinClearance=FMath::Min(Best.MinClearance,HumanWallDepth(Goals,Best.Bends[L]));
  }
  for(const auto& Pair:Limits)Best.MaxLimitViolation=FMath::Max(Best.MaxLimitViolation,Violation(Neutral[Pair.Key].GetRotation().Inverse()*Best.Local[Pair.Key].GetRotation(),Frames[Pair.Key],Pair.Value));
  const int32 Eyes=Bone(TEXT("PlayerEyes"));
  Best.Eye=Eyes>=0?Best.World.TransformPosition(Space[Eyes].GetLocation()):Best.World.TransformPosition(Space[Bone(TEXT("Head"))].GetLocation())+(Goals.Roots[2]+Goals.Roots[3]-Goals.Roots[0]-Goals.Roots[1]).GetSafeNormal()*12;
 }
 else
 {
  Shift=Wanted;Pitch=WantedPitch;Yaw=WantedYaw;
  float Ignored[4];
  Best=Pass(Goals,Shift,Candidate,false,Ignored,Pitch,Yaw,true);
  PoseFingers(Goals,Best);
 }
 PreviousWorld=Best.World;
 Previous.SetNum(Best.Local.Num());for(int32 I=0;I<Best.Local.Num();I++)Previous[I]=Best.Local[I].GetRotation();
 for(int32 L=0;L<4;L++)Swivel[L]=Candidate[L];HasHistory=Delta>0;LastPelvis=Pelvis;return Best;
}
