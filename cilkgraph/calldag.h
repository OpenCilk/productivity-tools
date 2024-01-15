#ifndef INCLUDED_CALLGRAPH_H
#define INCLUDED_CALLGRAPH_H

#include <vector>

#include <cilk/cilk.h>
#include <cilk/cilk_api.h>

namespace calldag {

enum class call_type {
  Root,
  Func,
  Task,
  Cont,
  Sync,
};

template <class OS>
OS& operator<<(OS& os, const call_type& type) {
  switch(type) {
    case call_type::Root:
      os << "R";
      break;
    case call_type::Func:
      os << "F";
      break;
    case call_type::Task:
      os << "T";
      break;
    case call_type::Cont:
      os << "C";
      break;
    case call_type::Sync:
      os << "S";
      break;
  }
  return os;
}

struct dot_diedge {
  std::string a, b;
};

template <class OS>
OS& operator<<(OS& os, dot_diedge e) {
  os << "\"" << e.a << "\" -> \"" << e.b << "\" ";
  return os;
}

struct node_t {
  call_type type;
  unsigned worker_id;
  std::vector<node_t*> children;

  node_t(call_type type, unsigned worker_id) :
    type(type), worker_id(worker_id) {
  }

  ~node_t() {
    for (auto& child : children) {
      delete child;
    }
  }

  template <class OS>
  void print_dot(OS& os, std::string& prefix) const {
    std::vector<std::string> active_nodes;
    std::string prev_node = prefix + "head";
    int nc = children.size();
    for (int i = 0; i < nc; ++i) {
      if (children[i]->type == call_type::Sync) {
        std::string sync_node = prefix + std::to_string(i) + ".sync";

        os << dot_diedge{prev_node, sync_node};
        for (auto& node : active_nodes) {
          os << dot_diedge{node, sync_node};
        }
        active_nodes.clear();

        prev_node = std::move(sync_node);
        continue;
      } else if (children[i]->type == call_type::Cont) {
        active_nodes.push_back(prev_node);
        prev_node = prefix + std::to_string(i) + ".cont";

        os << dot_diedge{prefix + "head", prev_node};
        continue;
      }

      int init_prefix_len = prefix.size();
      prefix += std::to_string(i);
      prefix.push_back('.');

      os << dot_diedge{prev_node, prefix + "head"};
      prev_node = prefix + "tail";

      children[i]->print_dot(os, prefix);
      prefix.resize(init_prefix_len);
    }
    os << dot_diedge{prev_node, prefix + "tail"};
  }

  template <class OS>
  void dump(OS& os) const {
    os << type << "[W" << worker_id << "](";
    for (auto& c : children) {
      c->dump(os);
    }
    os << ")";
  }

  template <class OS>
  friend OS& operator<<(OS& os, const node_t& node);
};

template <class OS>
OS& operator<<(OS& os, const node_t& node) {
#ifdef TRACE_CALLS
  node.dump(os);
  os << std::endl;
#endif
  std::string prefix;
  os << "digraph {";
  node.print_dot(os, prefix);
  os << "}";
  return os;
}

class dag_t {
  node_t root{call_type::Root, -1u};
  std::vector<node_t*> stack = {&root};

public:
  dag_t() {}
  dag_t(dag_t&& other) :
    root(std::move(other.root)), stack(std::move(other.stack))
  {
    stack[0] = &root;
  }

  void push(call_type type, unsigned worker_id) {
    stack.push_back(
        stack.back()->children.emplace_back(new node_t(type, worker_id)));
  }

  void pop() {
    stack.pop_back();
  }

  static void reduce(void* left_v, void* right_v) {
    auto left = static_cast<dag_t*>(left_v);
    auto right = static_cast<dag_t*>(right_v);

#ifdef TRACE_CALLS
    std::ostringstream ss;
    ss << "rhs: " << right->stack.size() << std::endl;
    std::cout << ss.str();
#endif

    auto& lchildren = left->stack.back()->children;
    auto& rchildren = right->root.children;

    lchildren.insert(
        lchildren.end(),
        std::make_move_iterator(rchildren.begin()),
        std::make_move_iterator(rchildren.end()));
    rchildren.clear();

    auto& lstack = left->stack;
    auto& rstack = right->stack;
    lstack.insert(
        lstack.end(),
        std::make_move_iterator(std::next(rstack.begin())),
        std::make_move_iterator(rstack.end()));
    rstack.clear();

    right->~dag_t();
  }

  static void identity(void* view) {
    new (view) dag_t();
  }

  template <class OS>
  friend OS& operator<<(OS& os, const dag_t& g);
};

template <class OS>
OS& operator<<(OS& os, const dag_t& g) {
  os << g.root;
  return os;
}

using dag_reducer = dag_t
    _Hyperobject(&dag_t::identity, &dag_t::reduce);

} //namespace calldag

#endif // INCLUDED_CALLGRAPH_H
