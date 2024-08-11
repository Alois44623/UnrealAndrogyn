// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundDocumentBuilderRegistry.h"

#include "HAL/PlatformProperties.h"
#include "MetasoundAssetManager.h"
#include "MetasoundGlobals.h"
#include "MetasoundSettings.h"
#include "MetasoundTrace.h"
#include "MetasoundUObjectRegistry.h"


namespace Metasound::Engine
{
	FDocumentBuilderRegistry::~FDocumentBuilderRegistry()
	{
		TMultiMap<FMetasoundFrontendClassName, TWeakObjectPtr<UMetaSoundBuilderBase>> BuildersToFinish;
		FScopeLock Lock(&BuildersCriticalSection);
		{
			BuildersToFinish = MoveTemp(Builders);
			Builders.Reset();
		}

		UE_CLOG(!BuildersToFinish.IsEmpty(), LogMetaSound, Display, TEXT("BuilderRegistry is shutting down with the following %i active builder entries. Forcefully shutting down:"), BuildersToFinish.Num());
		int32 NumStale = 0;
		for (const TPair<FMetasoundFrontendClassName, TWeakObjectPtr<UMetaSoundBuilderBase>>& Pair : BuildersToFinish)
		{
			const bool bIsValid = Pair.Value.IsValid();
			if (bIsValid)
			{
				UE_CLOG(bIsValid, LogMetaSound, Display, TEXT("- %s"), *Pair.Value->GetFullName());
				constexpr bool bForceUnregister = true;
				FinishBuildingInternal(*Pair.Value.Get(), bForceUnregister);
			}
			else
			{
				++NumStale;
			}
		}
		UE_CLOG(NumStale > 0, LogMetaSound, Display, TEXT("BuilderRegistry is shutting down with %i stale entries"), NumStale);
	}

	void FDocumentBuilderRegistry::AddBuilderInternal(const FMetasoundFrontendClassName& InClassName, UMetaSoundBuilderBase* NewBuilder) const
	{
		FScopeLock Lock(&BuildersCriticalSection);

// #if !NO_LOGGING
//		bool bLogDuplicateEntries = CanPostEventLog(ELogEvent::DuplicateEntries, ELogVerbosity::Error);
//		if (bLogDuplicateEntries)
//		{
//			bLogDuplicateEntries = Builders.Contains(InClassName);
//		}
// #endif // !NO_LOGGING

		Builders.Add(InClassName, NewBuilder);

// #if !NO_LOGGING
// 		if (bLogDuplicateEntries)
// 		{
// 			TArray<TWeakObjectPtr<UMetaSoundBuilderBase>> Entries;
// 			Builders.MultiFind(InClassName, Entries);
// 
// 			// Don't print stale entries as during cook and some editor asset actions,
// 			// these may be removed after a new valid builder is created.  If stale
// 			// entries leak, they will show up on registry logging upon destruction.
// 			Entries.RemoveAllSwap([](const TWeakObjectPtr<UMetaSoundBuilderBase>& Builder) { return !Builder.IsValid(); });
// 
// 			if (!Entries.IsEmpty())
// 			{
// 				UE_LOG(LogMetaSound, Error, TEXT("More than one asset registered with class name '%s'. "
// 					"Look-up may return builder that is not associated with desired object! \n"
// 					"This can happen if asset was moved using revision control and original location was revived. \n"
// 					"Remove all but one of the following assets and relink a duplicate or copied replacement asset:"),
// 					*InClassName.ToString());
// 				for (const TWeakObjectPtr<UMetaSoundBuilderBase>& BuilderPtr : Entries)
// 				{
// 					UE_LOG(LogMetaSound, Error, TEXT("- %s"), *BuilderPtr->GetConstBuilder().CastDocumentObjectChecked<UObject>().GetPathName());
// 				}
// 			}
// 		}
// #endif // !NO_LOGGING
	}

	bool FDocumentBuilderRegistry::CanPostEventLog(ELogEvent Event, ELogVerbosity::Type Verbosity) const
	{
#if NO_LOGGING
		return false;
#else // !NO_LOGGING

		if (const ELogVerbosity::Type* SetVerbosity = EventLogVerbosity.Find(Event))
		{
			return *SetVerbosity >= Verbosity;
		}

		return true;
#endif // !NO_LOGGING
	}

#if WITH_EDITORONLY_DATA
	FMetaSoundFrontendDocumentBuilder& FDocumentBuilderRegistry::FindOrBeginBuilding(TScriptInterface<IMetaSoundDocumentInterface> MetaSound)
	{
		UObject* Object = MetaSound.GetObject();
		check(Object);

		return FindOrBeginBuilding(*Object).GetBuilder();
	}
#endif // WITH_EDITORONLY_DATA

	FMetaSoundFrontendDocumentBuilder* FDocumentBuilderRegistry::FindBuilder(TScriptInterface<IMetaSoundDocumentInterface> MetaSound) const
	{
		if (UMetaSoundBuilderBase* Builder = FindBuilderObject(MetaSound))
		{
			return &Builder->GetBuilder();
		}

		return nullptr;
	}

	FMetaSoundFrontendDocumentBuilder* FDocumentBuilderRegistry::FindBuilder(const FMetasoundFrontendClassName& InClassName, const FTopLevelAssetPath& AssetPath) const
	{
		if (UMetaSoundBuilderBase* Builder = FindBuilderObject(InClassName, AssetPath))
		{
			return &Builder->GetBuilder();
		}

		return nullptr;
	}

	UMetaSoundBuilderBase* FDocumentBuilderRegistry::FindBuilderObject(TScriptInterface<const IMetaSoundDocumentInterface> MetaSound) const
	{
		UMetaSoundBuilderBase* FoundEntry = nullptr;
		if (const UObject* MetaSoundObject = MetaSound.GetObject())
		{
			const FMetasoundFrontendDocument& Document = MetaSound->GetConstDocument();
			const FMetasoundFrontendClassName& ClassName = Document.RootGraph.Metadata.GetClassName();
			TArray<TWeakObjectPtr<UMetaSoundBuilderBase>> Entries;

			{
				FScopeLock Lock(&BuildersCriticalSection);
				Builders.MultiFind(ClassName, Entries);
			}

			for (const TWeakObjectPtr<UMetaSoundBuilderBase>& BuilderPtr : Entries)
			{
				if (UMetaSoundBuilderBase* Builder = BuilderPtr.Get())
				{
					// Can be invalid if look-up is called during asset removal/destruction or the entry was
					// prematurely "finished". Only return invalid entry if builder asset path cannot be
					// matched as this is likely the destroyed entry associated with the provided AssetPath.
					const FMetaSoundFrontendDocumentBuilder& DocBuilder = Builder->GetConstBuilder();
					if (DocBuilder.IsValid())
					{
						UObject& TestMetaSound = BuilderPtr->GetConstBuilder().CastDocumentObjectChecked<UObject>();
						if (&TestMetaSound == MetaSoundObject)
						{
							FoundEntry = Builder;
							break;
						}
					}
					else
					{
						FoundEntry = Builder;
					}
				}
			}
		}

		return FoundEntry;
	}

	UMetaSoundBuilderBase* FDocumentBuilderRegistry::FindBuilderObject(const FMetasoundFrontendClassName& InClassName, const FTopLevelAssetPath& AssetPath) const
	{
		TArray<TWeakObjectPtr<UMetaSoundBuilderBase>> Entries;
		{
			FScopeLock Lock(&BuildersCriticalSection);
			Builders.MultiFind(InClassName, Entries);
		}

		UMetaSoundBuilderBase* FoundEntry = nullptr;
		for (const TWeakObjectPtr<UMetaSoundBuilderBase>& BuilderPtr : Entries)
		{
			if (UMetaSoundBuilderBase* Builder = BuilderPtr.Get())
			{
				const FMetaSoundFrontendDocumentBuilder& DocBuilder = Builder->GetConstBuilder();

				// Can be invalid if look-up is called during asset removal/destruction or the entry was
				// prematurely "finished". Only return invalid entry if builder asset path cannot be
				// matched as this is likely the destroyed entry associated with the provided AssetPath.
				if (DocBuilder.IsValid())
				{
					const UObject& DocObject = DocBuilder.CastDocumentObjectChecked<UObject>();
					FTopLevelAssetPath ObjectPath;
					if (ObjectPath.TrySetPath(&DocObject))
					{
						if (AssetPath.IsNull() || AssetPath == ObjectPath)
						{
							FoundEntry = Builder;
							break;
						}
					}
					else
					{
						FoundEntry = Builder;
					}
				}
				else
				{
					FoundEntry = Builder;
				}
			}
		}

		return FoundEntry;
	}

	TArray<UMetaSoundBuilderBase*> FDocumentBuilderRegistry::FindBuilderObjects(const FMetasoundFrontendClassName& InClassName) const
	{
		TArray<UMetaSoundBuilderBase*> FoundBuilders;
		TArray<TWeakObjectPtr<UMetaSoundBuilderBase>> Entries;

		{
			FScopeLock Lock(&BuildersCriticalSection);
			Builders.MultiFind(InClassName, Entries);
		}

		if (!Entries.IsEmpty())
		{
			Algo::TransformIf(Entries, FoundBuilders,
				[](const TWeakObjectPtr<UMetaSoundBuilderBase>& BuilderPtr) { return BuilderPtr.IsValid(); },
				[](const TWeakObjectPtr<UMetaSoundBuilderBase>& BuilderPtr) { return BuilderPtr.Get(); }
			);
		}

		return FoundBuilders;
	}

	FMetaSoundFrontendDocumentBuilder* FDocumentBuilderRegistry::FindOutermostBuilder(const UObject& InSubObject) const
	{
		using namespace Metasound::Frontend;
		TScriptInterface<IMetaSoundDocumentInterface> DocumentInterface = InSubObject.GetOutermostObject();
		check(DocumentInterface.GetObject());
		return FindBuilder(DocumentInterface);
	}

	bool FDocumentBuilderRegistry::FinishBuilding(const FMetasoundFrontendClassName& InClassName, bool bForceUnregisterNodeClass) const
	{
		using namespace Metasound;
		using namespace Metasound::Engine;

		TArray<UMetaSoundBuilderBase*> FoundBuilders = FindBuilderObjects(InClassName);
		for (UMetaSoundBuilderBase* Builder : FoundBuilders)
		{
			FinishBuildingInternal(*Builder, bForceUnregisterNodeClass);

		}

		FScopeLock Lock(&BuildersCriticalSection);
		return Builders.Remove(InClassName) > 0;
	}

	bool FDocumentBuilderRegistry::FinishBuilding(const FMetasoundFrontendClassName& InClassName, const FTopLevelAssetPath& AssetPath, bool bForceUnregisterNodeClass) const
	{
		using namespace Metasound;
		using namespace Metasound::Engine;

		TWeakObjectPtr<UMetaSoundBuilderBase> BuilderPtr;
		if (UMetaSoundBuilderBase* Builder = FindBuilderObject(InClassName, AssetPath))
		{
			FinishBuildingInternal(*Builder, bForceUnregisterNodeClass);
			BuilderPtr = TWeakObjectPtr<UMetaSoundBuilderBase>(Builder);
		}

		FScopeLock Lock(&BuildersCriticalSection);
		return Builders.RemoveSingle(InClassName, BuilderPtr) > 0;
	}

	void FDocumentBuilderRegistry::FinishBuildingInternal(UMetaSoundBuilderBase& Builder, bool bForceUnregisterNodeClass) const
	{
		using namespace Metasound;
		using namespace Metasound::Frontend;

		// If the builder has applied transactions to its document object that are not mirrored in the frontend registry,
		// unregister version in registry. This will ensure that future requests for the builder's associated asset will
		// register a fresh version from the object as the transaction history is intrinsically lost once this builder
		// is destroyed. It is also possible that the DocBuilder's underlying object can be invalid if object was force
		// deleted, so validity check is necessary.
		FMetaSoundFrontendDocumentBuilder& DocBuilder = Builder.GetBuilder();
		if (DocBuilder.IsValid())
		{
			if (Metasound::CanEverExecuteGraph())
			{
				const int32 TransactionCount = DocBuilder.GetTransactionCount();
				const int32 LastTransactionRegistered = Builder.GetLastTransactionRegistered();
				if (bForceUnregisterNodeClass || LastTransactionRegistered != TransactionCount)
				{
					UObject& MetaSound = DocBuilder.CastDocumentObjectChecked<UObject>();
					if (FMetasoundAssetBase* MetaSoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&MetaSound))
					{
						MetaSoundAsset->UnregisterGraphWithFrontend();
					}
				}
			}
			DocBuilder.FinishBuilding();
		}
	}

#if WITH_EDITOR
	FOnResolveAuditionPageInfo& FDocumentBuilderRegistry::GetOnResolveAuditionPageInfoDelegate()
	{
		return OnResolveAuditionPageInfo;
	}
#endif // WITH_EDITOR

	bool FDocumentBuilderRegistry::ReloadBuilder(const FMetasoundFrontendClassName& InClassName) const
	{
		bool bReloaded = false;
		TArray<UMetaSoundBuilderBase*> ClassBuilders = FindBuilderObjects(InClassName);
		for (UMetaSoundBuilderBase* Builder : ClassBuilders)
		{
			Builder->Reload();
			bReloaded = true;
		}

		return bReloaded;
	}

	FGuid FDocumentBuilderRegistry::ResolveTargetPageID(const FMetasoundFrontendDocument& Document, const FTopLevelAssetPath& AssetPath) const
	{
		FName PlatformName = FPlatformProperties::IniPlatformName();

#if WITH_EDITOR
		if (OnResolveAuditionPageInfo.IsBound())
		{
			FAuditionPageInfo PreviewInfo = OnResolveAuditionPageInfo.Execute(Document);
			if (PreviewInfo.PageID.IsSet())
			{
				return PreviewInfo.PageID.GetValue();
			}

			PlatformName = PreviewInfo.PlatformName;
		}
#endif // WITH_EDITOR

		TSet<FGuid> DocPageIds;
		Document.RootGraph.IterateGraphPages([&DocPageIds](const FMetasoundFrontendGraph& PageGraph)
		{
			DocPageIds.Add(PageGraph.PageID);
		});

		bool bImplementsPages = false;
		const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>();
		if (Settings)
		{
			const FGuid& TargetPageID = Settings->GetTargetPageID();
			const TArray<FMetaSoundPageSettings>& PageSettingsArray = Settings->GetPageSettings();
			bImplementsPages = !Settings->GetPageSettings().IsEmpty();

			bool bFoundMatch = false;
			for (int32 Index = PageSettingsArray.Num() - 1; Index >= 0; --Index)
			{
				const FMetaSoundPageSettings& PageSettings = PageSettingsArray[Index];
				bFoundMatch |= PageSettings.UniqueId == TargetPageID;
				const bool bAssetImplementsPage = DocPageIds.Contains(PageSettings.UniqueId);
				if (bFoundMatch && bAssetImplementsPage)
				{
#if WITH_EDITOR
					const bool bIsCooked = PageSettings.IsCooked.GetValueForPlatform(PlatformName);
					if (bIsCooked)
					{
						return PageSettings.UniqueId;
					}
#else // !WITH_EDITOR
					return PageSettings.UniqueId;
#endif // !WITH_EDITOR
				}
			}
		}

		// Set to arbitrary ID so the default is prioritized when iterating implemented
		// asset pages. All documents are guaranteed to implement at least one page.
		FGuid PageID = FGuid::NewGuid();
		Document.RootGraph.IterateGraphPages([&PageID](const FMetasoundFrontendGraph& Graph)
		{
			if (PageID.IsValid())
			{
				PageID = Graph.PageID;
			}
		});

#if !NO_LOGGING
		// Log error if page settings exist, but could not resolve ID with the given document
		if (bImplementsPages)
		{
			FString PageIdentifier = PageID.ToString();
			if (const FMetaSoundPageSettings* PageSettings = Settings->FindPageSettings(PageID))
			{
				PageIdentifier = PageSettings->Name.ToString();
			}

			UE_LOG(LogMetaSound, Error,
				TEXT("'%s' failed to resolve executable page ID:  \nMetaSound 'Page Settings' does not provide a valid fallback page for execution"
					" on the desired platform, which can result in undefined behavior. Registering asset's page with ID '%s'."),
				*AssetPath.ToString(),
				*PageIdentifier);
		}
#endif // !NO_LOGGING

		return PageID;
	}

	void FDocumentBuilderRegistry::SetEventLogVerbosity(ELogEvent Event, ELogVerbosity::Type Verbosity)
	{
		EventLogVerbosity.FindOrAdd(Event) = Verbosity;
	}
} // namespace Metasound::Engine