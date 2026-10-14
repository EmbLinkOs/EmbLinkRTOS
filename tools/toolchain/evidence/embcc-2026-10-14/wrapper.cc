template<class T>struct Box{T value;constexpr explicit Box(T x):value(x){} constexpr T get()const{return value;}};
extern "C" unsigned query(unsigned x){Box<unsigned>b(x);return b.get()+1;}
