#pragma once

#include <set>

#include <juce_audio_processors/juce_audio_processors.h>

namespace Virus
{
	class Controller;
}

namespace virusLib
{
	enum PresetVersion : uint8_t;
}

namespace genericVirusUI
{
	class VirusEditor;

	struct Patch
	{
	    int progNumber = 0;
	    juce::String name;
	    std::string category1;
	    std::string category2;
	    std::vector<uint8_t> sysex;
	    virusLib::PresetVersion model;
	    uint8_t unison = 0;
	    uint8_t transpose = 0;
	    uint8_t arpMode = 0;
	};

	class PatchBrowser : public juce::FileBrowserListener, juce::TableListBoxModel
	{
	public:
		explicit PatchBrowser(const VirusEditor& _editor);
		~PatchBrowser() override;

		static uint32_t load(const Virus::Controller& _controller, std::vector<Patch>& _result, std::set<std::string>* _dedupeChecksums, const std::vector<std::vector<uint8_t>>& _packets);
		static bool load(const Virus::Controller& _controller, std::vector<Patch>& _result, std::set<std::string>* _dedupeChecksums, const std::vector<uint8_t>& _data);
		static uint32_t loadBankFile(const Virus::Controller& _controller, std::vector<Patch>& _result, std::set<std::string>* _dedupeChecksums, const juce::File& file);

		bool selectPrevPreset();
		bool selectNextPreset();

	private:
		static bool initializePatch(const Virus::Controller& _controller, Patch& _patch);

	    juce::FileBrowserComponent& getBankList() {return m_bankList; }
	    juce::TableListBox& getPatchList() {return m_patchList; }
	    juce::TextEditor& getSearchBox() {return m_search; }

		void fitInParent(juce::Component& _component, const std::string& _parentName) const;

		// Inherited via FileBrowserListener
	    void selectionChanged() override;
	    void fileClicked(const juce::File &file, const juce::MouseEvent &e) override;
	    void fileDoubleClicked(const juce::File &file) override {}
	    void browserRootChanged(const juce::File &newRoot) override {}

	    // Inherited via TableListBoxModel
		int getNumRows() override;
		void paintRowBackground(juce::Graphics &, int rowNumber, int width, int height, bool rowIsSelected) override;
		void paintCell(juce::Graphics &, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
		void selectedRowsChanged(int lastRowSelected) override;
		void cellDoubleClicked (int rowNumber, int columnId, const juce::MouseEvent &) override;
	    void sortOrderChanged(int newSortColumnId, bool isForwards) override;

		void loadRomBank(uint32_t _bankIndex);

		void onFileSelected(const juce::File& file);
		void fillPatchList(const std::vector<Patch>& _patches);
		void refreshPatchList();

		bool selectPrevNextPreset(int _dir);

		class PatchBrowserSorter;

	    enum Columns
		{
	        INDEX = 1,
	        NAME = 2,
	        CAT1 = 3,
	        CAT2 = 4,
	        ARP = 5,
	        UNI = 6,
	        ST = 7,
	        VER = 8,
		};

		const VirusEditor& m_editor;
	    Virus::Controller& m_controller;
	    juce::WildcardFileFilter m_fileFilter;
	    juce::FileBrowserComponent m_bankList;
	    juce::TextEditor m_search;
	    juce::TableListBox m_patchList;
	    juce::Array<Patch> m_patches;
	    juce::Array<Patch> m_filteredPatches;
	    juce::PropertiesFile *m_properties;
		juce::ComboBox* m_romBankSelect;
	    juce::HashMap<juce::String, bool> m_checksums;
		bool m_sendOnSelect = true;
    };
}
