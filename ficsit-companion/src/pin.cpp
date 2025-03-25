#include "link.hpp"
#include "node.hpp"
#include "pin.hpp"
#include "recipe.hpp"

Pin::Pin(const ax::NodeEditor::PinId id, const ax::NodeEditor::PinKind direction,
    Node* node, const Item* item, const FractionalNumber& base_rate) :
    id(id), direction(direction), node(node), link(nullptr), item(item), base_rate(base_rate), current_rate(0, 1), error(false), locked(false)
{

}

Pin::~Pin()
{

}

void Pin::SetLocked(const bool b)
{
    if (locked == b)
    {
        return;
    }

    locked = b;
    if (link != nullptr)
    {
        Pin* linked_pin = direction == ax::NodeEditor::PinKind::Input ? link->start : link->end;
        if (linked_pin->locked != b)
        {
            linked_pin->SetLocked(b);
        }
    }

    switch (node->GetKind())
    {
    case Node::Kind::Craft:
    case Node::Kind::Group:
    case Node::Kind::GameSplitter:
        for (const auto& p : node->ins)
        {
            if (p->GetLocked() != b)
            {
                p->SetLocked(b);
            }
        }
        for (const auto& p : node->outs)
        {
            if (p->GetLocked() != b)
            {
                p->SetLocked(b);
            }
        }
        break;
    case Node::Kind::Merger:
    case Node::Kind::CustomSplitter:
    {
        const std::vector<std::unique_ptr<Pin>>& multi_pin = node->GetKind() == Node::Kind::CustomSplitter ? node->outs : node->ins;
        std::vector<Pin*> all_locked;
        std::vector<Pin*> all_unlocked;
        for (const auto& p : multi_pin)
        {
            (p->GetLocked() ? all_locked : all_unlocked).push_back(p.get());
        }

        // Single pin updated
        if ((direction == ax::NodeEditor::PinKind::Input && node->GetKind() == Node::Kind::CustomSplitter) ||
            (direction == ax::NodeEditor::PinKind::Output && node->GetKind() == Node::Kind::Merger)
        )
        {
            // If locked and only one unlock multi pin remaining, lock it
            if (b && all_unlocked.size() == 1)
            {
                all_unlocked[0]->SetLocked(b);
            }
            // If unlocked and all multi pin are locked, unlock all multi pin as otherwise the state is invalid
            if (!b && all_unlocked.size() == 0)
            {
                for (const auto& p : multi_pin)
                {
                    p->SetLocked(b);
                }
            }
        }
        // Multi pin updated
        else
        {
            Pin* single_pin = (node->GetKind() == Node::Kind::CustomSplitter ? node->ins : node->outs)[0].get();
            // If everything is locked, lock single pin
            if (all_unlocked.size() == 0)
            {
                if (!single_pin->GetLocked())
                {
                    single_pin->SetLocked(true);
                }
            }
            // If we just locked this pin, the single pin is locked and there's only one multi pin unlocked, lock the last multi pin
            else if (b && single_pin->GetLocked() && all_unlocked.size() == 1)
            {
                all_unlocked[0]->SetLocked(b);
            }
            // If we just unlocked this pin and the single pin was locked, it's now unlocked
            else if (!b && single_pin->GetLocked() && all_unlocked.size() == 1)
            {
                if (single_pin->GetLocked())
                {
                    single_pin->SetLocked(b);
                }
            }
        }
    }
        break;
    case Node::Kind::Sink:
        break;
    }
}

bool Pin::GetLocked() const
{
    return locked;
}
