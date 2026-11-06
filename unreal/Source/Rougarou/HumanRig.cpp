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
FHumanAngles FHumanRig::Measure(FQuat Relative,const FHumanFrame& Frame)
{
 Relative.Normalize();
 const FVector D=(Frame.F^Frame.T).GetSafeNormal(), E=(Frame.S^Frame.T).GetSafeNormal();
 const FVector Turned=Relative.RotateVector(Frame.T).GetSafeNormal();
 FHumanAngles Result;
 Result.Swing=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Turned,Frame.T),-1.,1.)));
 Result.Bearing=FMath::Atan2(FVector::DotProduct(Turned,E),FVector::DotProduct(Turned,D));
 const FQuat Twist=FQuat::FindBetweenNormals(Frame.T,Turned).Inverse()*Relative;
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
 return (FQuat::FindBetweenNormals(Frame.T,Direction)*FQuat(Frame.T,FMath::DegreesToRadians(Twist/Frame.Side))).GetNormalized();
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
  FVector Over=TipPanel.Up;
  FString Grip=TEXT("free");
  if(Arm)
  {
   if(Hold)
   {
    Grip=Hold->Kind;
    Over=(-TipPanel.Right*Hold->Pull.X-TipPanel.Up*Hold->Pull.Y).GetSafeNormal();
    if(!Contact)Grip=TEXT("free");
   }
   else if(Sim.Smeared[L]&&Contact)Grip=TEXT("smear");
   else Over=(At-Result.Roots[L]).GetSafeNormal();
   if(Contact && Hold)Result.HandSurfaces[L-2]=Hold->KnuckleContact;
   if(Contact && Hold && Hold->KnuckleContact.IsSet())
    Result.Ends[L]=Hold->KnuckleContact.GetValue()-Over*(Grip==TEXT("jug")?12.2f:10.f)+TipPanel.Out*2.2;
   else Result.Ends[L]=At-Over*9+TipPanel.Out*(Grip==TEXT("crimp")?6.3f:4.6f);
   Result.Along[L]=Over;
  }
  else
  {
   if(Contact && Hold)Result.ShoeSurfaces[L]=Hold->ShoeContact;
   const FVector Shoe=Result.ShoeSurfaces[L].Get(At);
   Result.Ends[L]=Shoe+TipPanel.Out*19+TipPanel.Up*4.7;
   Result.Along[L]=(Shoe-Result.Ends[L]).GetSafeNormal();
  }
  Result.Out[L]=TipPanel.Out;Result.Grips[L]=Grip;Result.Held[L]=Contact;
  Result.Poles[L]=Arm ? Panel.Out*.6f+Panel.Right*(Sign*.5f)-Panel.Up*.6f
   : FRig::Knee(Panel.Right,Panel.Up,Panel.Out,Sign,(Result.Ends[L]-Result.Roots[L]).Z);
 }
 Result.Look=-Panel.Out;
 return Result;
}
float FHumanPose::Cost(const FHumanGoals& Goals) const
{
 if(!Finite)return 1.e9f;
 float Result=Comfort*.12f;
 for(int32 L=0;L<4;L++)Result+=FMath::Square(Miss[L])*(Goals.Held[L]?10.f:.15f);
 Result+=FMath::Square(FMath::Max(0.f,2-MinClearance))*50;
 return Result;
}
FString FHumanRig::ProfilePath(){return FPaths::ProjectContentDir()/TEXT("Data/human_motion.json");}
int32 FHumanRig::Bone(const TCHAR* Name) const
{
 const int32* Index=Indices.Find(FName(Name));return Index?*Index:INDEX_NONE;
}
void FHumanRig::Compose(const TArray<FTransform>& Local,TArray<FTransform>& Space) const
{
 Space.SetNumUninitialized(Local.Num());
 for(int32 I=0;I<Local.Num();I++)Space[I]=Parents[I]<0?Local[I]:Local[I]*Space[Parents[I]];
}
void FHumanRig::Reset(){HasHistory=false;Pitch=0;Yaw=0;Previous.Reset();Shift=FVector::ZeroVector;for(float& A:Swivel)A=0;}

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
 Compose(Rest,RestSpace);
 auto P=[&](const TCHAR* N){return RestSpace[Bone(N)].GetLocation();};
 RefPelvis=(P(TEXT("LeftUpLeg"))+P(TEXT("RightUpLeg")))/2;
 RefUp=((P(TEXT("LeftArm"))+P(TEXT("RightArm")))/2-RefPelvis).GetSafeNormal();
 RefSide=(P(TEXT("LeftArm"))-P(TEXT("RightArm"))).GetSafeNormal();
 RefFront=FVector::VectorPlaneProject(P(TEXT("LeftToeBase"))-P(TEXT("LeftFoot"))+P(TEXT("RightToeBase"))-P(TEXT("RightFoot")),RefUp).GetSafeNormal();
 Neutral=Rest;
 float Drop=32;
 FString Json;TSharedPtr<FJsonObject> Data;
 if(FFileHelper::LoadFileToString(Json,*Profile))FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data);
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
  Frames.Add(I,{Inv.RotateVector(T),Inv.RotateVector(F),Inv.RotateVector(S),Left?-1.f:1.f});
 }
 return true;
}
FQuat FHumanRig::LimitLocal(int32 Index,FQuat Local) const
{
 const FHumanLimit* L=Limits.Find(Index);const FHumanFrame* Frame=Frames.Find(Index);
 if(!LimitsEnabled||!L||!Frame)return Local.GetNormalized();
 return (Neutral[Index].GetRotation()*Clamp(Neutral[Index].GetRotation().Inverse()*Local,*Frame,*L)).GetNormalized();
}

static float HumanWallDepth(const FHumanGoals& Goals,FVector Point)
{
 float Result=MAX_flt;
 for(const FClimbPanel& P:Goals.Panels)
 {
  const float Along=FVector::DotProduct(Point-P.Origin,P.Up)/100;
  if(Along<-.15f||Along>P.Length+.15f)continue;
  const float Depth=FVector::DotProduct(Point-P.Origin,P.Out);
  if(Depth> -60)Result=FMath::Min(Result,Depth);
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
 const float Lateral=FMath::DegreesToRadians(12.f*(Lift[0]-Lift[1]));
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
    const float Angle=FMath::Clamp(FMath::Asin(FMath::Clamp(FVector::DotProduct(Direction,RefUp),0.,1.))*Mix.Scapula,0.,double(FMath::DegreesToRadians(25.f)));
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
    return C;
   };
   float Psi=Iteration==0?Swivels[Limb]:Chosen[Limb];FCandidate Best=Attempt(Psi);
   if(Search && Iteration==0)
   {
    for(int32 I=0;I<36;I++)
    {
     const float A=-PI+2*PI*I/36;FCandidate C=Attempt(A);
     if(C.Cost<Best.Cost-.001f){Psi=A;Best=C;}
    }
    for(float H:{.12f,.05f,.02f})for(float Direction:{-1.f,1.f})
    {const float A=Psi+H*Direction;FCandidate C=Attempt(A);if(C.Cost<Best.Cost){Psi=A;Best=C;}}
   }
   Chosen[Limb]=Psi;Result.Local[U].SetRotation(Best.Upper);Result.Local[L].SetRotation(Best.Lower);UpdateBranch(U);
   // Share pronation/supination with the forearm instead of wringing the wrist.
   const FVector Forward=Arm?RestSpace[TipIndex].GetLocation()-RestSpace[E].GetLocation():Sole;
   const FVector Desired=Result.World.InverseTransformVectorNoScale(Goals.Along[Limb]);
   const FVector Facing=Result.World.InverseTransformVectorNoScale(Arm?-Goals.Out[Limb]:Goals.Panels.IsEmpty()?FVector::UpVector:Goals.Panels[0].Up);
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

void FHumanRig::PoseFingers(const FHumanGoals& Goals,FHumanPose& Pose) const
{
 // Shoes articulate around the imported ball-of-foot bone, not the ankle.
 for(int32 L=0;L<2;++L)
 {
  const int32 Toe=Bone(L==0?TEXT("RightToeBase"):TEXT("LeftToeBase"));
  if(Toe<0)continue;
  const FVector Axis=RestSpace[Toe].GetRotation().Inverse().RotateVector(RefSide);
  Pose.Local[Toe].SetRotation((Rest[Toe].GetRotation()*FQuat(Axis,FMath::DegreesToRadians(Goals.Held[L]?8.f:-4.f))).GetNormalized());
 }
 TArray<FTransform> Space;
 Compose(Pose.Local,Space);
 for(int32 Limb=2;Limb<4;++Limb)
 {
  const TCHAR* Side=Limb==2?TEXT("Right"):TEXT("Left");
  const FString& Grip=Goals.Grips[Limb];
  const FVector Over=Pose.World.InverseTransformVectorNoScale(Goals.Along[Limb]);
  const FVector In=Pose.World.InverseTransformVectorNoScale(-Goals.Out[Limb]);
  for(const TCHAR* Finger:{TEXT("Index"),TEXT("Middle"),TEXT("Ring"),TEXT("Pinky")})
  {
   int32 Chain[3];
   for(int32 J=0;J<3;++J)Chain[J]=Bone(*FString::Printf(TEXT("%sHand%s%d"),Side,Finger,J+1));
   if(Chain[0]<0||Chain[1]<0||Chain[2]<0)continue;
   for(int32 J=0;J<3;++J)
   {
    const int32 I=Chain[J],Parent=Parents[I];
    const FVector RestAxis=J<2?RestSpace[Chain[J+1]].GetLocation()-RestSpace[I].GetLocation()
     :RestSpace[I].GetLocation()-RestSpace[Parent].GetLocation();
    float Bend=8.f*(J+1);
    if(Grip==TEXT("jug")) Bend=J==0?16.f:J==1?74.f:105.f;
    else if(Grip==TEXT("crimp")) Bend=J==0?22.f:J==1?95.f:84.f;
    else if(Grip==TEXT("pinch")) Bend=J==0?20.f:J==1?45.f:64.f;
    else if(Grip==TEXT("sloper")||Grip==TEXT("smear")) Bend=4.f*J;
    if(Grip==TEXT("free"))continue;
    const float Angle=FMath::DegreesToRadians(Bend);
    const FVector Aim=(Over*FMath::Cos(Angle)+In*FMath::Sin(Angle)).GetSafeNormal();
    const FQuat Carried=(Space[Parent].GetRotation()*Rest[I].GetRotation()).GetNormalized();
    const FVector Axis=Carried.RotateVector(RestSpace[I].GetRotation().Inverse().RotateVector(RestAxis)).GetSafeNormal();
    const FQuat Rotation=FQuat::FindBetweenNormals(Axis,Aim)*Carried;
    Pose.Local[I].SetRotation((Space[Parent].GetRotation().Inverse()*Rotation).GetNormalized());
    Space[I]=Pose.Local[I]*Space[Parent];
   }
  }

  for(int32 J=1;J<=3;++J)
  {
   const int32 I=Bone(*FString::Printf(TEXT("%sHandThumb%d"),Side,J));
   if(I<0||Grip==TEXT("free"))continue;
   const FVector Axis=RestSpace[I].GetRotation().Inverse().RotateVector(Over^In).GetSafeNormal();
   Pose.Local[I].SetRotation((Rest[I].GetRotation()*FQuat(Axis,FMath::DegreesToRadians(J==1?15.f:22.f))).GetNormalized());
  }
 }
}

// One direct pass: the hips and chest sit where the sim puts them, each limb is
// a two-bone solve aimed at its pole, and joint limits clamp the result. Only
// the inputs are smoothed, so the same goals always give the same pose and
// there is nothing for frame-to-frame searching to jitter on.
FHumanPose FHumanRig::Solve(const FHumanGoals& InputGoals,float Delta)
{
 if(UseLegacy)return SolveLegacy(InputGoals,Delta);
 if(!Ready())return {};
 const FVector Pelvis=(InputGoals.Roots[0]+InputGoals.Roots[1])/2;
 if(HasHistory && FVector::Dist(Pelvis,LastPelvis)>100)Reset();
 if(!HasHistory || Delta<=0 || Delta>=.25f)SmoothGoals=InputGoals;
 else
 {
  const float Dt=FMath::Min(Delta,.05f);
  const float Body=1-FMath::Exp(-10*Dt), Reach=1-FMath::Exp(-18*Dt), Turn=1-FMath::Exp(-5*Dt);
  FHumanGoals Next=InputGoals;
  for(int32 L=0;L<4;++L)
  {
   Next.Roots[L]=FMath::Lerp(SmoothGoals.Roots[L],InputGoals.Roots[L],Body);
   Next.Ends[L]=FMath::Lerp(SmoothGoals.Ends[L],InputGoals.Ends[L],Reach);
   // knees and elbows swing over slowly, so a high step never snaps into a drop knee
   Next.Poles[L]=FMath::Lerp(SmoothGoals.Poles[L],InputGoals.Poles[L],Turn);


  }
  for(int32 L=0;L<2;++L)
  {
   if(InputGoals.ShoeSurfaces[L].IsSet() && SmoothGoals.ShoeSurfaces[L].IsSet())Next.ShoeSurfaces[L]=FMath::Lerp(SmoothGoals.ShoeSurfaces[L].GetValue(),InputGoals.ShoeSurfaces[L].GetValue(),Reach);
   if(InputGoals.HandSurfaces[L].IsSet() && SmoothGoals.HandSurfaces[L].IsSet())Next.HandSurfaces[L]=FMath::Lerp(SmoothGoals.HandSurfaces[L].GetValue(),InputGoals.HandSurfaces[L].GetValue(),Reach);
  }
  Next.Look=FMath::Lerp(SmoothGoals.Look,InputGoals.Look,Turn).GetSafeNormal();
  SmoothGoals=MoveTemp(Next);
 }
 const float Zero[4]={};float Ignored[4];
 // Short limbs pull the hips toward the held contacts: two direct passes, the
 // second shifted by the average miss, never a search over candidate shifts.
 const bool Settle=HasHistory && Delta>0 && Delta<.25f;
 FVector Wanted=Shift;
 // Newton steps on the contact cost along each body axis: a smooth function of
 // the goals, unlike a pattern search whose answer jumps between grid points.
 const FVector Axes[3]={SmoothGoals.Out[0],(SmoothGoals.Roots[3]-SmoothGoals.Roots[2]).GetSafeNormal(),(SmoothGoals.Roots[2]+SmoothGoals.Roots[3]-SmoothGoals.Roots[0]-SmoothGoals.Roots[1]).GetSafeNormal()};
 auto Score=[&](FVector At){return Pass(SmoothGoals,At,Zero,false,Ignored,0,0,true).Cost(SmoothGoals);};
 const float H=1.5f;
 for(int32 Iteration=0;Iteration<(Settle?2:8);++Iteration)
 {
  const float Here=Score(Wanted);FVector Step=FVector::ZeroVector;
  for(const FVector& Axis:Axes)
  {
   if(Axis.IsNearlyZero())continue;
   const float Plus=Score(Wanted+Axis*H),Minus=Score(Wanted-Axis*H);
   const float Slope=(Plus-Minus)/(2*H),Curve=(Plus-2*Here+Minus)/(H*H);
   Step-=Axis*FMath::Clamp(Curve>1e-3f?Slope/Curve:Slope*.05f,-4.f,4.f);
  }
  Wanted=(Wanted+Step).GetClampedToMaxSize(MaxShift);
  const float Depth=HumanWallDepth(SmoothGoals,Pelvis+Wanted);
  if(Depth<18)Wanted+=SmoothGoals.Out[0]*(18-Depth);
 }
 Shift=Settle?FMath::Lerp(Shift,Wanted,1-FMath::Exp(-8*FMath::Min(Delta,.05f))):Wanted;
 FHumanPose Best=Pass(SmoothGoals,Shift,Zero,false,Ignored,0,0,true);
 PoseFingers(SmoothGoals,Best);
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
 Pitch=Yaw=0;
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
