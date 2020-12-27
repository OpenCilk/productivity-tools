// -*- C++ -*-
#ifndef __DICTIONARY__
#define __DICTIONARY__

typedef DisjointSet_t<SPBagInterface *> * value_type00;
typedef MemoryAccess_t value_type00;
// struct value_type00 {
//   std::shared_ptr<MemoryAccess_t> val;

//   value_type00() : val(nullptr) {}

//   value_type00(MemoryAccess_t acc)
//       : val(std::make_shared<MemoryAccess_t>(acc))
//   {}

//   value_type00(const value_type00 &copy)
//       : val(copy.val)
//   {}

//   value_type00(const value_type00 &&move)
//       : val(std::move(move.val))
//   {}

//   value_type00 &operator=(const value_type00 &copy) {
//     val = copy.val;
//     return *this;
//   }

//   value_type00 &operator=(const value_type00 &&move) {
//     val = std::move(move.val);
//     return *this;
//   }

//   bool isValid() const {
//     return (bool)val && val->isValid();
//   }

//   void invalidate() {
//     return val.reset();
//   }

//   bool operator==(const value_type00 &that) const {
//     if (val == that.val)
//       return true;
//     if (((bool)val && !(bool)that.val) ||
//         (!(bool)val && (bool)that.val))
//       return false;
//     return *val == *that.val;
//   }

//   bool operator!=(const value_type00 &that) const {
//     return !(val == that.val);
//   }
// };

class Dictionary {
public:
  static const value_type00 null_val;

  virtual value_type00 *find(uint64_t key) {
    return nullptr;
  }

  virtual value_type00 *find_group(uint64_t key, size_t max_size,
                                   size_t &num_elems) {
    num_elems = 1;
    return nullptr;
  }

  virtual value_type00 *find_exact_group(uint64_t key, size_t max_size,
                                         size_t &num_elems) {
    num_elems = 1;
    return nullptr;
  }

  virtual const value_type00 &operator[] (uint64_t key) {
    return null_val;
  }

  virtual void erase(uint64_t key) {}

  virtual void erase(uint64_t key, size_t size) {}

  virtual bool includes(uint64_t key) {
    return false;
  }

  virtual bool includes(uint64_t key, size_t size) {
    return false;
  }

  virtual void insert(uint64_t key, const value_type00 &f) {}

  virtual void insert(uint64_t key, size_t size, const value_type00 &f) {}
  virtual void set(uint64_t key, size_t size, value_type00 &&f) {
    insert(key, size, std::move(f));
  }
  virtual void insert_into_found_group(uint64_t key, size_t size,
                                       value_type00 *dst,
                                       value_type00 &&f) {
    insert(key, size, std::move(f));
  }

  virtual ~Dictionary() {};

  //uint32_t run_length(uint64_t key) {return 0;}

  //virtual void destruct() {}
};

#endif  // __DICTIONARY__
