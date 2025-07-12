#pragma once

#include <optional>
#include <cstdio>


template <class TContext, class TWriter>
class BitstreamWriter
{
  TContext mCtx;
  TWriter mWriter;

public:
  BitstreamWriter(TContext&& ctx, TWriter&& writer)
    : mCtx{std::move(ctx)}
    , mWriter{std::move(writer)}
  {
  }

  template <class T>
  bool write(const T& val)
  {
    return mWriter(mCtx, (const void*)&val, sizeof(val));
  }
  template <class T>
  bool write(const T* val, size_t cnt)
  {
    return mWriter(mCtx, (const void*)val, sizeof(*val) * cnt);
  }

  const TContext& ctx() const { return mCtx; }
};

template <class TContext, class TReader>
class BitstreamReader
{
  TContext mCtx;
  TReader mReader;

public:
  BitstreamReader(TContext&& ctx, TReader&& reader)
    : mCtx{std::move(ctx)}
    , mReader{std::move(reader)}
  {
  }

  template <class T>
  std::optional<T> read()
  {
    T val;
    bool res = mReader(mCtx, (void*)&val, sizeof(val));
    if (res)
      return val;
    else
      return std::nullopt;
  }

  template <class T>
  bool read(T* val, size_t cnt)
  {
    return mReader(mCtx, (void*)val, sizeof(*val) * cnt);
  }

  const TContext& ctx() const { return mCtx; }
};

struct FileContext
{
  FILE* mf;

public:
  explicit FileContext(FILE* f)
    : mf{f}
  {
  }

  FileContext(const FileContext&) = delete;
  FileContext& operator=(const FileContext&) = delete;

  FileContext(FileContext&& other)
    : mf{std::exchange(other.mf, nullptr)}
  {
  }
  FileContext& operator=(FileContext&& other)
  {
    this->~FileContext();
    new (this) FileContext{std::move(other)};
    return *this;
  }

  ~FileContext()
  {
    if (mf)
    {
      fclose(mf);
      mf = nullptr;
    }
  }

  operator FILE*() const { return mf; }
};

inline auto make_binfile_writer(const char* path)
{
  auto res =
    BitstreamWriter{FileContext{fopen(path, "wb")}, [](FileContext& f, const void* val, size_t sz) {
                      return fwrite(val, sz, 1, f) == 1;
                    }};
  using ReturnType = std::optional<decltype(res)>;
  if (res.ctx())
    return ReturnType{std::move(res)};
  else
    return ReturnType{std::nullopt};
}

inline auto make_binfile_reader(const char* path)
{
  auto res =
    BitstreamReader{FileContext{fopen(path, "rb")}, [](FileContext& f, void* val, size_t sz) {
                      return fread(val, sz, 1, f) == 1;
                    }};
  using ReturnType = std::optional<decltype(res)>;
  if (res.ctx())
    return ReturnType{std::move(res)};
  else
    return ReturnType{std::nullopt};
}
