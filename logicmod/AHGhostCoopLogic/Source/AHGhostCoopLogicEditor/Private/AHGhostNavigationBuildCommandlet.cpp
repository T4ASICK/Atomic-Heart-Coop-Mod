#include "AHGhostNavigationBuildCommandlet.h"
#include "AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/ISlateRHIRendererModule.h"
#include "Modules/ModuleManager.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "UObject/UnrealType.h"
#include "Engine/Font.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "WidgetBlueprint.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace
{
bool SaveAsset(UPackage* Package,UObject* Asset)
{
    FAssetRegistryModule::AssetCreated(Asset);
    Package->MarkPackageDirty();
    const FString File=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
    return UPackage::SavePackage(Package,Asset,RF_Public|RF_Standalone,*File,GError,nullptr,false,true,SAVE_NoError);
}
bool BuildMenu()
{
    UPackage* Package=CreatePackage(TEXT("/Game/Mods/AHGhostCoopNavigation/WBP_GhostMenu"));
    UWidgetBlueprint* Widget=CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(),Package,TEXT("WBP_GhostMenu"),BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass()));
    FEdGraphPinType Integer; Integer.PinCategory=UEdGraphSchema_K2::PC_Int;
    FBlueprintEditorUtils::AddMemberVariable(Widget,TEXT("Request"),Integer,TEXT("0"));
    UBorder* Root=Widget->WidgetTree->ConstructWidget<UBorder>();
    Root->SetBrushColor(FLinearColor(.025f,.035f,.05f,.98f)); Root->SetPadding(FMargin(24));
    USizeBox* Frame=Widget->WidgetTree->ConstructWidget<USizeBox>();
    Frame->SetWidthOverride(600); Frame->SetHeightOverride(640); Frame->AddChild(Root);
    Widget->WidgetTree->RootWidget=Frame;
    UVerticalBox* Rows=Widget->WidgetTree->ConstructWidget<UVerticalBox>(); Root->AddChild(Rows);
    UObject* Font=LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    auto Text=[&](const TCHAR* Name,const TCHAR* Label,int32 Size)
    {
        UTextBlock* Block=Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(Name));
        Block->bIsVariable=true; Block->SetFont(FSlateFontInfo(Font,Size,TEXT("Regular")));
        Block->SetText(FText::FromString(Label)); Block->SetAutoWrapText(true);
        return Block;
    };
    Rows->AddChildToVerticalBox(Text(TEXT("Title"),TEXT("AH Ghost Co-op"),26))->SetPadding(FMargin(0,0,0,12));
    USizeBox* StatusBox=Widget->WidgetTree->ConstructWidget<USizeBox>();
    StatusBox->SetHeightOverride(52); StatusBox->SetClipping(EWidgetClipping::ClipToBounds);
    auto* Status=Text(TEXT("StatusText"),TEXT(""),16);
    FindFProperty<FFloatProperty>(UTextBlock::StaticClass(),TEXT("WrapTextAt"))->SetPropertyValue_InContainer(Status,548.0f);
    StatusBox->AddChild(Status);
    Rows->AddChildToVerticalBox(StatusBox)->SetPadding(FMargin(0,0,0,8));
    // Preserve action IDs so older menu packages cannot invoke a different command.
    const int32 ActionIds[]={1,5,6};
    const TCHAR* Labels[]={TEXT("Закрыть меню · Home"),
        TEXT("Показать / скрыть сетевого игрока"),TEXT("Телепорт к сетевому игроку")};
    static_assert(UE_ARRAY_COUNT(ActionIds)==UE_ARRAY_COUNT(Labels),"Each menu label needs an action ID");
    TArray<UButton*> Buttons;
    for (int32 Index=0;Index<UE_ARRAY_COUNT(Labels);++Index)
    {
        UButton* Button=Widget->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),FName(*FString::Printf(TEXT("Action%d"),ActionIds[Index])));
        Button->bIsVariable=true;
        auto* Label=Text(*FString::Printf(TEXT("Label%d"),ActionIds[Index]),Labels[Index],20);
        Label->SetAutoWrapText(false); Label->SetJustification(ETextJustify::Center);
        auto* Content=CastChecked<UButtonSlot>(Button->AddChild(Label));
        Content->SetHorizontalAlignment(HAlign_Fill); Content->SetVerticalAlignment(VAlign_Center); Content->SetPadding(FMargin(12,6));
        Button->SetBackgroundColor(FLinearColor(.10f,.18f,.23f,1));
        USizeBox* Row=Widget->WidgetTree->ConstructWidget<USizeBox>(); Row->SetHeightOverride(52); Row->AddChild(Button);
        auto* Slot=Rows->AddChildToVerticalBox(Row); Slot->SetPadding(FMargin(0,3)); Slot->SetHorizontalAlignment(HAlign_Fill);
        Buttons.Add(Button);
    }
    Rows->AddChildToVerticalBox(Text(TEXT("Hint"),TEXT("Home — открыть или закрыть меню.\nСетевой игрок появляется автоматически."),14))->SetPadding(FMargin(0,12,0,0));
    FKismetEditorUtilities::CompileBlueprint(Widget);
    UEdGraph* Graph=FBlueprintEditorUtils::FindEventGraph(Widget);
    if (!Graph) return false;
    auto* Schema=CastChecked<UEdGraphSchema_K2>(Graph->GetSchema());
    for (int32 Index=0;Index<Buttons.Num();++Index)
    {
        auto* Property=FindFProperty<FObjectProperty>(Widget->GeneratedClass,Buttons[Index]->GetFName());
        auto* Delegate=FindFProperty<FMulticastDelegateProperty>(UButton::StaticClass(),TEXT("OnClicked"));
        if (!Property || !Delegate) return false;
        auto* Event=NewObject<UK2Node_ComponentBoundEvent>(Graph);
        Graph->AddNode(Event,true,false); Event->CreateNewGuid();
        Event->InitializeComponentBoundEventParams(Property,Delegate); Event->AllocateDefaultPins();
        auto* Set=NewObject<UK2Node_VariableSet>(Graph);
        Graph->AddNode(Set,true,false); Set->CreateNewGuid(); Set->VariableReference.SetSelfMember(TEXT("Request")); Set->AllocateDefaultPins();
        Set->FindPinChecked(TEXT("Request"))->DefaultValue=FString::FromInt(ActionIds[Index]);
        if (!Schema->TryCreateConnection(Event->FindPinChecked(UEdGraphSchema_K2::PN_Then),Set->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Widget);
    if (Widget->Status==BS_Error) return false;
    // Exercise real generated widget bindings, not just the graph description.
    UUserWidget* Instance=NewObject<UUserWidget>(GetTransientPackage(),Widget->GeneratedClass);
    if (!Instance->Initialize()) return false;
    auto* Request=FindFProperty<FIntProperty>(Instance->GetClass(),TEXT("Request"));
    if (!Request) return false;
    for (int32 Index=0;Index<Buttons.Num();++Index)
    {
        auto* Button=Cast<UButton>(Instance->GetWidgetFromName(FName(*FString::Printf(TEXT("Action%d"),ActionIds[Index]))));
        if (!Button) return false;
        Request->SetPropertyValue_InContainer(Instance,0);
        Button->OnClicked.Broadcast();
        if (Request->GetPropertyValue_InContainer(Instance)!=ActionIds[Index]) return false;
    }
    CastChecked<UTextBlock>(Instance->GetWidgetFromName(TEXT("StatusText")))->SetText(FText::FromString(TEXT("Сетевой игрок: виден")));
    FWidgetRenderer Renderer(true);
    auto SlateWidget=Instance->TakeWidget();
    auto* Target=Renderer.DrawWidget(SlateWidget,FVector2D(600,640));
    FlushRenderingCommands();
    TArray<FColor> Pixels;
    if (!Target || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)) return false;
    TArray<uint8> PNG; FImageUtils::CompressImageArray(600,640,Pixels,PNG);
    if (!FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("GhostMenu-preview.png")))) return false;
    float PreviousBottom=0;
    for (int32 Index=0;Index<Buttons.Num();++Index)
    {
        auto* Button=Instance->GetWidgetFromName(FName(*FString::Printf(TEXT("Action%d"),ActionIds[Index])));
        auto* Label=Instance->GetWidgetFromName(FName(*FString::Printf(TEXT("Label%d"),ActionIds[Index])));
        const FGeometry Geometry=Button->GetPaintSpaceGeometry();
        const float Top=Geometry.GetAbsolutePosition().Y,Bottom=Top+Geometry.GetAbsoluteSize().Y;
        if (Geometry.GetLocalSize().Y<50 || Top<PreviousBottom || Bottom>640 ||
            Label->GetDesiredSize().X>Label->GetPaintSpaceGeometry().GetLocalSize().X ||
            Label->GetDesiredSize().Y>Label->GetPaintSpaceGeometry().GetLocalSize().Y)
        {
            UE_LOG(LogTemp,Error,TEXT("Menu layout row%d top%f bottom%f local%s labelDesired%s labelActual%s"),
                Index,Top,Bottom,*Geometry.GetLocalSize().ToString(),*Label->GetDesiredSize().ToString(),*Label->GetPaintSpaceGeometry().GetLocalSize().ToString());
            return false;
        }
        PreviousBottom=Bottom;
    }
    UE_LOG(LogTemp,Display,TEXT("Ghost menu layout self-test: three non-overlapping 52px buttons; labels fit; saved 600x640 Slate render"));
    UE_LOG(LogTemp,Display,TEXT("Ghost menu self-test: all 3 generated button events deliver action IDs 1, 5 and 6"));
    return SaveAsset(Package,Widget);
}
bool BuildOpaqueWeapon()
{
    UPackage* Package=CreatePackage(TEXT("/Game/Mods/AHGhostCoopNavigation/M_GhostWeaponOpaque"));
    UMaterial* Material=NewObject<UMaterial>(Package,TEXT("M_GhostWeaponOpaque"),RF_Public|RF_Standalone);
    Material->BlendMode=BLEND_Opaque; Material->bDisableDepthTest=false;
    auto* Color=NewObject<UMaterialExpressionConstant3Vector>(Material);
    Color->Constant=FLinearColor(.075f,.095f,.11f); Material->Expressions.Add(Color); Material->BaseColor.Expression=Color;
    Material->Metallic.Constant=.65f; Material->Roughness.Constant=.42f;
    Material->PostEditChange();
    if (Material->GetBlendMode()!=BLEND_Opaque || Material->bDisableDepthTest) return false;
    UE_LOG(LogTemp,Display,TEXT("Ghost Dominator material self-test: opaque blend, depth testing enabled"));
    return SaveAsset(Package,Material);
}
}

UAHGhostNavigationBuildCommandlet::UAHGhostNavigationBuildCommandlet()
{
    IsClient=true; // Layout preview needs client textures and the Slate renderer.
    IsEditor=true;
    LogToConsole=true;
}

int32 UAHGhostNavigationBuildCommandlet::Main(const FString&)
{
    if (!FSlateApplication::IsInitialized())
        FSlateApplication::InitializeAsStandaloneApplication(
            FModuleManager::LoadModuleChecked<ISlateRHIRendererModule>(TEXT("SlateRHIRenderer")).CreateSlateRHIRenderer());
    UPackage* Package=CreatePackage(TEXT("/Game/Mods/AHGhostCoopNavigation/WBP_GhostTag"));
    UWidgetBlueprint* Widget=CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(),Package,TEXT("WBP_GhostTag"),BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass()));
    USizeBox* Root=Widget->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TagRoot"));
    Root->SetWidthOverride(300); Root->SetHeightOverride(90);
    Widget->WidgetTree->RootWidget=Root;
    UVerticalBox* Rows=Widget->WidgetTree->ConstructWidget<UVerticalBox>();
    Root->AddChild(Rows);
    UObject* Font=LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    if (!Font) return 1;
    for (const FName Name : {FName(TEXT("NameText")),FName(TEXT("DistanceText"))})
    {
        UTextBlock* Text=Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
        Text->bIsVariable=true;
        Text->SetFont(FSlateFontInfo(Font,Name==TEXT("NameText") ? 22 : 16,TEXT("Bold")));
        Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.25f,0.95f,0.95f,1)));
        Text->SetShadowColorAndOpacity(FLinearColor::Black); Text->SetShadowOffset(FVector2D(1,2));
        Text->SetJustification(ETextJustify::Center); Text->SetAutoWrapText(true);
        Text->SetText(FText::FromString(Name==TEXT("NameText") ? TEXT("Friend") : TEXT("0 m")));
        Rows->AddChildToVerticalBox(Text)->SetHorizontalAlignment(HAlign_Fill);
    }
    FKismetEditorUtilities::CompileBlueprint(Widget);
    if (Widget->Status==BS_Error) return 2;
    FAssetRegistryModule::AssetCreated(Widget);
    Package->MarkPackageDirty();
    const FString File=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
    if (!UPackage::SavePackage(Package,Widget,RF_Public|RF_Standalone,*File,GError,nullptr,false,true,SAVE_NoError)) return 3;
    return BuildMenu() && BuildOpaqueWeapon() ? 0 : 4;
}
