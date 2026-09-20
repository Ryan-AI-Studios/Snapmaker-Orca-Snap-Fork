#ifndef slic3r_GUI_SlotRemapDialog_hpp_
#define slic3r_GUI_SlotRemapDialog_hpp_

#include "libslic3r/SlotRemap.hpp"

#include <wx/choice.h>
#include <wx/dialog.h>

#include <vector>

namespace Slic3r { namespace GUI {

class SlotRemapDialog : public wxDialog
{
public:
    SlotRemapDialog(
        wxWindow                         *parent,
        const std::vector<unsigned int>  &source_ids,
        const std::vector<std::string>   &source_colours,
        const std::vector<std::string>   &dest_colours,
        const SlotRemapMap               &initial);

    SlotRemapMap result_map() const;

private:
    std::vector<unsigned int> m_sources;
    std::vector<wxChoice *>   m_choices;
};

}} // namespace Slic3r::GUI

#endif
