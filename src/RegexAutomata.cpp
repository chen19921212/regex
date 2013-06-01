#include "RegexAutomata.h"
#include "RegexParser.h"
#include <vector>
#include <queue>
#include <set>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <assert.h>

namespace regex
{
    namespace automata
    {
        using namespace parser;

        bool operator < (const Edge &left, const Edge &right)
        {
            // We assume left and right are equivalent when left is
            // right's subset or right is left's subset, so we return false.
            if (left.first_ >= right.first_ && left.last_ <= right.last_)
                return false;
            else if (right.first_ >= left.first_ && right.last_ <= left.last_)
                return false;
            else
                return left.last_ < right.first_;
        }

        class EdgeSet
        {
        public:
            typedef std::set<Edge> EdgeContainer;
            typedef EdgeContainer::iterator Iterator;

            EdgeSet() { }
            EdgeSet(const EdgeSet &) = delete;
            void operator = (const EdgeSet &) = delete;

            Iterator Begin() const
            {
                return edges_.begin();
            }

            Iterator End() const
            {
                return edges_.end();
            }

            std::pair<Iterator, Iterator> Search(int c) const
            {
                return Search(c, c);
            }

            std::pair<Iterator, Iterator> Search(int first, int last) const
            {
                return Search(Edge(first, last));
            }

            std::pair<Iterator, Iterator> Search(const Edge &range) const
            {
                Edge first(range.first_, range.first_);
                Edge last(range.last_, range.last_);
                return std::make_pair(edges_.lower_bound(first),
                                      edges_.upper_bound(last));
            }

            void Insert(int c)
            {
                Insert(Edge(c, c));
            }

            void Insert(int first, int last)
            {
                Insert(Edge(first, last));
            }

            void Insert(const Edge &range)
            {
                int current = range.first_;
                int last = range.last_;

                while (current <= last)
                {
                    Edge cur(current, current);
                    Iterator it = edges_.lower_bound(cur);
                    if (it == edges_.end())
                    {
                        edges_.insert(Edge(current, last));
                        current = last + 1;
                    }
                    else
                    {
                        if (current < it->first_)
                        {
                            if (last < it->first_)
                            {
                                edges_.insert(Edge(current, last));
                                current = last + 1;
                            }
                            else
                            {
                                edges_.insert(Edge(current, it->first_ - 1));
                                current = it->first_;
                            }
                        }
                        else if (current == it->first_)
                        {
                            if (last < it->last_)
                            {
                                int back_first = last + 1;
                                int back_last = it->last_;
                                edges_.erase(it);
                                edges_.insert(Edge(current, last));
                                edges_.insert(Edge(back_first, back_last));
                                current = last + 1;
                            }
                            else
                            {
                                current = it->last_ + 1;
                            }
                        }
                        else
                        {
                            if (last < it->last_)
                            {
                                int front_first = it->first_;
                                int front_last = current - 1;
                                int middle_first = current;
                                int middle_last = last;
                                int back_first = last + 1;
                                int back_last = it->last_;
                                edges_.erase(it);
                                edges_.insert(Edge(front_first, front_last));
                                edges_.insert(Edge(middle_first, middle_last));
                                edges_.insert(Edge(back_first, back_last));
                                current = last + 1;
                            }
                            else
                            {
                                int front_first = it->first_;
                                int front_last = current - 1;
                                int back_first = current;
                                int back_last = it->last_;
                                edges_.erase(it);
                                edges_.insert(Edge(front_first, front_last));
                                edges_.insert(Edge(back_first, back_last));
                                current = back_last + 1;
                            }
                        }
                    }
                }
            }

        private:
            EdgeContainer edges_;
        };

        // Visitor class construct edge set from AST
        class EdgeSetConstructorVisitor : public Visitor
        {
        public:
            explicit EdgeSetConstructorVisitor(EdgeSet *edge_set)
                : edge_set_(edge_set)
            {
            }

            EdgeSetConstructorVisitor(const EdgeSetConstructorVisitor &) = delete;
            void operator = (const EdgeSetConstructorVisitor &) = delete;

            VISIT_NODE(CharNode);
            VISIT_NODE(CharRangeNode);
            VISIT_NODE(ConcatenationNode) { }
            VISIT_NODE(AlternationNode) { }
            VISIT_NODE(ClosureNode) { }

        private:
            EdgeSet *edge_set_;
        };

        void EdgeSetConstructorVisitor::Visit(CharNode *ast, void *data)
        {
            edge_set_->Insert(ast->c_);
        }

        void EdgeSetConstructorVisitor::Visit(CharRangeNode *ast, void *data)
        {
            edge_set_->Insert(ast->first_, ast->last_);
        }

        // Template class to map (NodeType, EdgeType) to NodeType
        template<typename NodeType, typename EdgeType>
        class NodeMap
        {
        public:
            NodeMap() { }
            NodeMap(const NodeMap &) = delete;
            void operator = (const NodeMap &) = delete;

            // Get first node-edge mapped node
            const NodeType * Map(const NodeType *node, const EdgeType *edge)
            {
                auto key = std::make_pair(node, edge);
                auto it = map_.find(key);
                if (it != map_.end())
                    return it->second;
                else
                    return nullptr;
            }

            // Add node1-edge-node2 map
            void Set(const NodeType *node1, const EdgeType *edge, const NodeType *node2)
            {
                auto key = std::make_pair(node1, edge);
                map_.insert(std::make_pair(key, node2));
            }

        private:
            struct NodeEdgeHash
            {
                std::size_t operator () (const std::pair<const NodeType *,
                                                         const EdgeType *> &pair) const
                {
                    return std::hash<const NodeType *>()(pair.first) *
                           std::hash<const EdgeType *>()(pair.second);
                }
            };

            std::unordered_multimap<std::pair<const NodeType *, const EdgeType *>,
                                    const NodeType *, NodeEdgeHash> map_;
        };

        class NFA
        {
        public:
            explicit NFA(std::unique_ptr<EdgeSet> edge_set)
                : edge_set_(std::move(edge_set)), start_(nullptr), accept_(nullptr)
            {
            }

            NFA(const NFA &) = delete;
            void operator = (const NFA &) = delete;

            typedef std::vector<std::unique_ptr<Node>> NodeList;
            typedef EdgeSet::Iterator EdgeIterator;

            void SetMap(const Node *node1, const Edge *edge, const Node *node2)
            {
                node_map_.Set(node1, edge, node2);
            }

            const Node * Map(const Node *node, const Edge *edge)
            {
                return node_map_.Map(node, edge);
            }

            const Node * AddNode(Node::Type t = Node::Type_Normal)
            {
                auto index = nodes_.size();
                auto node = new Node(index, t);
                nodes_.push_back(std::unique_ptr<Node>(node));
                return node;
            }

            void SetStartNode(const Node *node)
            {
                start_ = node;
                const_cast<Node *>(start_)->type_ = Node::Type_Start;
            }

            const Node * GetStartNode() const
            {
                return start_;
            }

            void SetAcceptNode(const Node *node)
            {
                accept_ = node;
                const_cast<Node *>(accept_)->type_ = Node::Type_Accept;
            }

            const Node * GetAcceptNode() const
            {
                return accept_;
            }

            std::size_t NodeCount() const
            {
                return nodes_.size();
            }

            // Get node by index
            const Node * GetNode(std::size_t index) const
            {
                if (index < nodes_.size())
                    return nodes_[index].get();
                else
                    return nullptr;
            }

            // Get epsilon edge
            const Edge * GetEpsilon() const
            {
                return &epsilon_;
            }

            // Get edge iterator range
            std::pair<EdgeIterator, EdgeIterator> SearchEdge(int c) const
            {
                return edge_set_->Search(c);
            }

            std::pair<EdgeIterator, EdgeIterator> SearchEdge(int first, int last) const
            {
                return edge_set_->Search(first, last);
            }

            // Get all edges begin and end iterator pair without epsilon edge
            std::pair<EdgeIterator, EdgeIterator> GetEdgeIterators() const
            {
                return std::make_pair(edge_set_->Begin(), edge_set_->End());
            }

        private:
            // All NFA nodes
            NodeList nodes_;
            // Epsilon edge
            Edge epsilon_;
            // Non-epsilon edges
            std::unique_ptr<EdgeSet> edge_set_;
            // Node-Edge map
            NodeMap<Node, Edge> node_map_;

            // Start and accept node
            const Node *start_;
            const Node *accept_;
        };

        // Visitor for convert AST to NFA
        class NFAConverterVisitor : public Visitor
        {
        public:
            typedef std::pair<const Node *, const Node *> DataType;

            explicit NFAConverterVisitor(NFA *nfa) : nfa_(nfa) { }

            NFAConverterVisitor(const NFAConverterVisitor &) = delete;
            void operator = (const NFAConverterVisitor &) = delete;

            VISIT_NODE(CharNode);
            VISIT_NODE(CharRangeNode);
            VISIT_NODE(ConcatenationNode);
            VISIT_NODE(AlternationNode);
            VISIT_NODE(ClosureNode);

        private:
            void FillData(void *data, const Node *node1, const Node *node2)
            {
                auto pair = reinterpret_cast<DataType *>(data);
                pair->first = node1;
                pair->second = node2;
            }

            NFA *nfa_;
        };

        void NFAConverterVisitor::Visit(CharNode *ast, void *data)
        {
            auto node1 = nfa_->AddNode();
            auto node2 = nfa_->AddNode();

            auto edges = nfa_->SearchEdge(ast->c_);
            for (; edges.first != edges.second; ++edges.first)
            {
                nfa_->SetMap(node1, &(*edges.first), node2);
            }

            FillData(data, node1, node2);
        }

        void NFAConverterVisitor::Visit(CharRangeNode *ast, void *data)
        {
            auto node1 = nfa_->AddNode();
            auto node2 = nfa_->AddNode();

            auto edges = nfa_->SearchEdge(ast->first_, ast->last_);
            for (; edges.first != edges.second; ++edges.first)
            {
                nfa_->SetMap(node1, &(*edges.first), node2);
            }

            FillData(data, node1, node2);
        }

        void NFAConverterVisitor::Visit(ConcatenationNode *ast, void *data)
        {
            assert(!ast->nodes_.empty());

            const Node *first = nullptr;
            const Node *last = nullptr;

            DataType pair;
            ast->nodes_[0]->Accept(this, &pair);
            first = pair.first;
            last = pair.second;

            for (std::size_t i = 1; i < ast->nodes_.size(); ++i)
            {
                ast->nodes_[i]->Accept(this, &pair);
                nfa_->SetMap(last, nfa_->GetEpsilon(), pair.first);
                last = pair.second;
            }

            FillData(data, first, last);
        }

        void NFAConverterVisitor::Visit(AlternationNode *ast, void *data)
        {
            std::vector<const Node *> all_first;
            std::vector<const Node *> all_last;

            for (auto it = ast->nodes_.begin(); it != ast->nodes_.end(); ++it)
            {
                DataType pair;
                (*it)->Accept(this, &pair);
                all_first.push_back(pair.first);
                all_last.push_back(pair.second);
            }

            auto node1 = nfa_->AddNode();
            for (auto it = all_first.begin(); it != all_first.end(); ++it)
                nfa_->SetMap(node1, nfa_->GetEpsilon(), *it);

            auto node2 = nfa_->AddNode();
            for (auto it = all_last.begin(); it != all_last.end(); ++it)
                nfa_->SetMap(*it, nfa_->GetEpsilon(), node2);

            FillData(data, node1, node2);
        }

        void NFAConverterVisitor::Visit(ClosureNode *ast, void *data)
        {
            auto node1 = nfa_->AddNode();
            auto node2 = nfa_->AddNode();

            DataType pair;
            ast->node_->Accept(this, &pair);

            nfa_->SetMap(pair.second, nfa_->GetEpsilon(), pair.first);
            nfa_->SetMap(node1, nfa_->GetEpsilon(), pair.first);
            nfa_->SetMap(pair.second, nfa_->GetEpsilon(), node2);
            nfa_->SetMap(node1, nfa_->GetEpsilon(), node2);

            FillData(data, node1, node2);
        }

        std::unique_ptr<NFA> ConvertASTToNFA(const std::unique_ptr<ASTNode> &ast)
        {
            // Construct EdgeSet
            std::unique_ptr<EdgeSet> edge_set(new EdgeSet);

            EdgeSetConstructorVisitor edge_set_visitor(edge_set.get());
            ast->Accept(&edge_set_visitor, nullptr);

            // Construct NFA
            std::unique_ptr<NFA> nfa(new NFA(std::move(edge_set)));

            NFAConverterVisitor visitor(nfa.get());
            NFAConverterVisitor::DataType pair;
            ast->Accept(&visitor, &pair);

            nfa->SetStartNode(pair.first);
            nfa->SetAcceptNode(pair.second);

            return std::move(nfa);
        }

        // Bit set for subset construction
        class BitSet
        {
            friend struct BitSetHash;
        public:
            explicit BitSet(std::size_t elem_count);

            // Set has index element
            void Set(std::size_t index);

            // Is index element set
            bool IsSet(std::size_t index) const;

            // Get element count of set
            std::size_t ElemCount() const
            {
                return elem_count_;
            }

            friend bool operator == (const BitSet &left,
                                     const BitSet &right)
            {
                return left.elem_count_ == right.elem_count_ &&
                       left.bits_ == right.bits_;
            }

            friend bool operator != (const BitSet &left,
                                     const BitSet &right)
            {
                return !(left == right);
            }

        private:
            static const std::size_t kUnsignedIntBits = 32;
            std::size_t GetIntCount(std::size_t elem_count);

            std::size_t elem_count_;
            std::vector<unsigned int> bits_;
        };

        BitSet::BitSet(std::size_t elem_count)
            : elem_count_(elem_count),
              bits_(GetIntCount(elem_count))
        {
        }

        void BitSet::Set(std::size_t index)
        {
            std::size_t unsigned_int_index = index / kUnsignedIntBits;
            std::size_t bit_index = index % kUnsignedIntBits;

            if (unsigned_int_index < bits_.size())
                bits_[unsigned_int_index] |= 0x1 >> bit_index;
        }

        bool BitSet::IsSet(std::size_t index) const
        {
            std::size_t unsigned_int_index = index / kUnsignedIntBits;
            std::size_t bit_index = index % kUnsignedIntBits;

            if (unsigned_int_index < bits_.size())
                return (bits_[unsigned_int_index] & (0x1 >> bit_index)) != 0;
            else
                return false;
        }

        std::size_t BitSet::GetIntCount(std::size_t elem_count)
        {
            return elem_count / kUnsignedIntBits +
                  (elem_count % kUnsignedIntBits == 0 ? 0 : 1);
        }

        struct BitSetHash
        {
            std::size_t operator () (const BitSet &bitset) const
            {
                std::hash<unsigned int> h;

                std::size_t result = 1;
                for (auto it = bitset.bits_.begin(); it != bitset.bits_.end(); ++it)
                    result *= h(*it);

                return result;
            }
        };

        // Set of BitSet for subset construction
        class BitSetSet
        {
        public:
            BitSetSet() { }

            BitSetSet(const BitSetSet &) = delete;
            void operator = (const BitSetSet &) = delete;

            // Add a BitSet, return the pointer of BitSet in the
            // BitSetSet and the bitset is existed or not
            std::pair<const BitSet *, bool> AddBitSet(const BitSet &bitset)
            {
                auto pair = bitsets_.insert(bitset);
                return std::make_pair(&(*pair.first), !pair.second);
            }

        private:
            std::unordered_set<BitSet, BitSetHash> bitsets_;
        };

        // Construct DFA from NFA
        class DFAConstructor
        {
        public:
            explicit DFAConstructor(std::unique_ptr<NFA> nfa);

            DFAConstructor(const DFAConstructor &) = delete;
            void operator = (const DFAConstructor &) = delete;

        private:
            // Construct epsilon closure
            void ConstructEpsilonClosure();

            // Epsilon closure the bitset
            void EpsilonClosure(BitSet &bitset);

            // Subset construct start BitSet from NFA
            const BitSet * ConstructStartBitSet();

            // First value of return pair is next BitSet from 'q' BitSet through edge,
            // second value of return pair indicate first value of return pair need add
            // to work list or not
            std::pair<const BitSet *, bool> ConstructDelta(const BitSet *q,
                                                           const Edge *edge);

            // Construct subsets_ from NFA
            void SubsetConstruction();

            std::unique_ptr<NFA> nfa_;
            BitSetSet subsets_;
        };

        DFAConstructor::DFAConstructor(std::unique_ptr<NFA> nfa)
            : nfa_(std::move(nfa))
        {
        }

        void DFAConstructor::ConstructEpsilonClosure()
        {
        }

        void DFAConstructor::EpsilonClosure(BitSet &bitset)
        {
        }

        const BitSet * DFAConstructor::ConstructStartBitSet()
        {
            BitSet q0(nfa_->NodeCount());
            q0.Set(nfa_->GetStartNode()->index_);
            EpsilonClosure(q0);
            return subsets_.AddBitSet(q0).first;
        }

        std::pair<const BitSet *, bool> DFAConstructor::ConstructDelta(const BitSet *q,
                                                                       const Edge *edge)
        {
            assert(nfa_->NodeCount() == q->ElemCount());
            BitSet t(nfa_->NodeCount());

            auto count = q->ElemCount();
            for (std::size_t i = 0; i < count; ++i)
            {
                if (q->IsSet(i))
                {
                    auto node1 = nfa_->GetNode(i);
                    auto node2 = nfa_->Map(node1, edge);
                    if (node2)
                        t.Set(node2->index_);
                }
            }

            EpsilonClosure(t);
            auto pair = subsets_.AddBitSet(t);

            return std::make_pair(pair.first, !pair.second);
        }

        void DFAConstructor::SubsetConstruction()
        {
            BitSetSet subsets;
            auto q0 = ConstructStartBitSet();

            std::queue<const BitSet *> work_list;
            work_list.push(q0);

            auto edge_iter_pair = nfa_->GetEdgeIterators();

            NodeMap<BitSet, Edge> node_map;
            while (!work_list.empty())
            {
                auto q = work_list.front();
                work_list.pop();

                for (auto it = edge_iter_pair.first;
                     it != edge_iter_pair.second; ++it)
                {
                    auto pair = ConstructDelta(q, &(*it));
                    node_map.Set(q, &(*it), pair.first);

                    if (pair.second)
                        work_list.push(pair.first);
                }
            }
        }
    } // namespace automata
} // namespace regex
