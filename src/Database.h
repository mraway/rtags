#ifndef Database_h
#define Database_h

#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <Serializer.h>
#include "Location.h"
#include "CursorInfo.h"
#include "ReadWriteLock.h"

struct Slice
{
    Slice(const std::string &str);
    Slice(const ByteArray &d);
    Slice(const char *d = 0, int s = -1);
    const char *data() const;
    int size() const;
    void clear();
    bool operator==(const Slice &other) const;
    bool operator!=(const Slice &other) const;
    ByteArray byteArray() const { return ByteArray(data(), size()); }
private:
    Slice(const leveldb::Slice &slice);
    leveldb::Slice mSlice;
    friend class Database;
    friend class Iterator;
    friend struct Batch;
};

static inline Log &operator<<(Log &dbg, const Slice &slice)
{
    dbg << std::string(slice.data(), slice.size()).c_str();
    return dbg;
}

template <typename T> ByteArray encode(const T &t)
{
    ByteArray out;
    Serializer s(out);
    s << t;
    return out;
}

template <typename T> T decode(const Slice &slice)
{
    Deserializer ds(slice.data(), slice.size());
    T t;
    ds >> t;
    return t;
}

template <> inline ByteArray encode(const Set<Location> &locations)
{
    ByteArray out(locations.size() * sizeof(uint64_t), '\0');
    uint64_t *ptr = reinterpret_cast<uint64_t*>(out.data());
    for (Set<Location>::const_iterator it = locations.begin(); it != locations.end(); ++it) {
        *ptr++ = (*it).mData;
    }
    return out;
}

template <> inline Set<Location> decode(const Slice &slice)
{
    Set<Location> ret;
    const uint64_t *ptr = reinterpret_cast<const uint64_t*>(slice.data());
    const int count = slice.size() / sizeof(uint64_t);
    for (int i=0; i<count; ++i) {
        ret.insert(Location(*ptr++));
    }
    return ret;
}

template <typename T> inline void writeNativeType(char *&data, T t)
{
    *reinterpret_cast<T*>(data) = t;
    data += sizeof(T);
}

template <> inline ByteArray encode(const CursorInfo &info)
{
    int size = info.symbolName.size() + 1; // null terminated symbolName
    size += sizeof(unsigned char); // symbolLength
    size += sizeof(int16_t); // kind, negative if not definition
    size += sizeof(uint64_t); // target
    size += sizeof(uint16_t); // references count
    size += sizeof(uint64_t) * info.references.size(); // references
    size += sizeof(uint64_t) * info.additionalReferences.size(); // references
    ByteArray out(size, '\0');
    char *data = out.data();
    memcpy(data, info.symbolName.constData(), info.symbolName.size() + 1);
    data += (info.symbolName.size() + 1);
    if (info.symbolName.contains("foobar"))
        printf("[%s] %s:%d: pos %ld\n", __func__, __FILE__, __LINE__, data - out.data());
    writeNativeType<unsigned char>(data, info.symbolLength);
    if (info.symbolName.contains("foobar"))
        printf("[%s] %s:%d: pos %ld\n", __func__, __FILE__, __LINE__, data - out.data());
    writeNativeType<int16_t>(data, info.isDefinition ? static_cast<int16_t>(info.kind) : -static_cast<int16_t>(info.kind));
    if (info.symbolName.contains("foobar"))
        printf("[%s] %s:%d: pos %ld\n", __func__, __FILE__, __LINE__, data - out.data());
    writeNativeType<uint64_t>(data, info.target.mData);
    if (info.symbolName.contains("foobar"))
        printf("[%s] %s:%d: pos %ld\n", __func__, __FILE__, __LINE__, data - out.data());
    writeNativeType<uint16_t>(data, info.references.size());
    if (info.symbolName.contains("foobar"))
        printf("[%s] %s:%d: pos %ld\n", __func__, __FILE__, __LINE__, data - out.data());
    for (Set<Location>::const_iterator it = info.references.begin(); it != info.references.end(); ++it)
        writeNativeType<uint64_t>(data, it->mData);
    if (info.symbolName.contains("foobar"))
        printf("[%s] %s:%d: pos %ld\n", __func__, __FILE__, __LINE__, data - out.data());
    for (Set<Location>::const_iterator it = info.additionalReferences.begin(); it != info.additionalReferences.end(); ++it) {
        writeNativeType<uint64_t>(data, it->mData);
    }
    if (info.symbolName.contains("foobar"))
        printf("[%s] %s:%d: pos %ld\n", __func__, __FILE__, __LINE__, data - out.data());
    if (info.symbolName.contains("foobar"))
        error() << "encoded " << info.symbolName << (data - out.data()) << " " << size;
    // error() << "encoded " << size << " " << (data - out.data) << " "; // << info;

    return out;
}

template <typename T> inline T readNativeType(const char *&data)
{
    const T ret = *reinterpret_cast<const T*>(data);
    data += sizeof(T);
    return ret;
}

template <> inline CursorInfo decode(const Slice &slice)
{
    CursorInfo ret;
    ret.symbolName = ByteArray(slice.data()); // 0-terminated
    const char *data = slice.data() + ret.symbolName.size() + 1;
    ret.symbolLength = readNativeType<unsigned char>(data);
    int16_t kindAndDefinition = readNativeType<int16_t>(data);
    if (kindAndDefinition > 0) {
        ret.isDefinition = true;
        ret.kind = static_cast<CXCursorKind>(kindAndDefinition);
    } else {
        ret.isDefinition = false;
        ret.kind = static_cast<CXCursorKind>(-kindAndDefinition);
    }
    ret.target.mData = readNativeType<uint64_t>(data);
    uint16_t refCount = readNativeType<uint16_t>(data);
    for (uint16_t i=0; i<refCount; ++i) {
        const Location loc(readNativeType<uint64_t>(data));
        ret.references.insert(loc);
    }

    int pos = data - slice.data();
    int remainingBytes = slice.size() - pos;
    for (int i=0; i<remainingBytes; i+=sizeof(uint64_t)) {
        const Location loc(readNativeType<uint64_t>(data));
        ret.additionalReferences.insert(loc);
    }
    if (ret.symbolName.contains("foobar"))
        error() << "decoded " << ret.symbolName << " " << slice.size();

    return ret;
}


class Iterator
{
    Iterator(leveldb::Iterator *iterator);
public:
    ~Iterator();
    void seekToFirst();
    void seekToLast();
    bool isValid() const;
    void next();
    void previous();
    Slice key() const;
    Slice rawValue() const;
    void seek(const Slice &slice);
    template <typename T> T value() const { return decode<T>(rawValue()); }
private:
    leveldb::Iterator *mIterator;
    friend class Database;
};

class LocationComparator;
class Database
{
public:
    enum { Version = 14 };

    enum Flag {
        NoFlag = 0x0,
        LocationKeys = 0x1
    };
    Database(const Path &path, int cacheSizeMB, unsigned flags);
    ~Database();

    unsigned flags() const { return mFlags; }
    void lock(ReadWriteLock::LockType type) { mLock.lock(type); }
    void unlock() { mLock.unlock(); }
    bool isOpened() const;
    void close();
    ByteArray openError() const;
    std::string rawValue(const Slice &key, bool *ok = 0) const;
    template <typename T> T value(const Slice &key, bool *ok = 0) {
        const std::string val = rawValue(key, ok);
        if (!val.empty())
            return decode<T>(val);
        return T();
    }
    int setRawValue(const Slice &key, const Slice &value);
    template <typename T> int setValue(const Slice &key, const T &t) { return setRawValue(key, encode(t)); }
    bool contains(const Slice &key) const;
    void remove(const Slice &key);
    Iterator *createIterator() const;
    Path path() const { return mPath; }
private:
    ReadWriteLock mLock;
    leveldb::DB *mDB;
    Path mPath;
    const leveldb::WriteOptions mWriteOptions;
    ByteArray mOpenError;
    LocationComparator *mLocationComparator;
    const unsigned mFlags;
    friend struct Batch;
};

class ScopedDB;
struct Batch {
    enum { BatchThreshold = 1024 * 1024 };
    Batch(ScopedDB &d);
    ~Batch();
    int flush();
    template <typename T> int add(const Slice &key, const T &t)
    {
        const ByteArray encoded = encode<T>(t);
        return addEncoded(key, Slice(encoded));
    }

    void remove(const Slice &key);
    int size() const { return mSize; }
    int total() const { return mTotal; }
    int addEncoded(const Slice &key, const Slice &data);
private:
    Database *mDB;
    int mSize, mTotal;
    leveldb::WriteBatch mBatch;
};


#endif
