#pragma once

/* Bridge between FluCoMa's client layer and avendish.
 *
 * FluCoMa clients (flucoma-core/include/flucoma/clients/rt|nrt) describe their
 * parameters as constexpr descriptor tuples (defineParameters(FloatParam(...)...))
 * and expose a uniform process() interface. This header maps:
 *
 *  - each parameter descriptor to an avnd control port (fluid_param<Client, N>),
 *    pulling name, default and Min()/Max() constraints at compile time;
 *  - avnd audio buffers to the std::vector<FluidTensorView<T,1>> that RT
 *    clients consume (FluidRTHost);
 *  - avnd setup/tick lifecycle to FluidContext / sampleRate / reset.
 */

#include <boost/pfr.hpp>

#include <flucoma/clients/common/FluidBaseClient.hpp>
#include <flucoma/clients/common/FluidContext.hpp>
#include <flucoma/clients/common/ParameterSet.hpp>
#include <flucoma/clients/common/ParameterTypes.hpp>
#include <flucoma/data/TensorTypes.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace FluCoMa
{

// --- compile-time constraint extraction -----------------------------------

namespace detail
{
// FluCoMa constraint functors are minimal structs; Min()/Max() are the only
// ones carrying a static bound we can surface as a UI range.
template <template <typename> class Impl, typename T>
struct is_specialization : std::false_type
{
};
template <template <typename> class Impl, typename T>
struct is_specialization<Impl, Impl<T>> : std::true_type
{
};

template <template <typename> class Impl, typename Tuple, std::size_t... Is>
constexpr auto find_bound_impl(const Tuple& t, std::index_sequence<Is...>)
    -> std::optional<double>
{
  std::optional<double> res;
  auto check = [&]<typename C>(const C& c) {
    if constexpr(is_specialization<Impl, std::decay_t<C>>::value)
      res = static_cast<double>(c.value);
  };
  (check(std::get<Is>(t)), ...);
  return res;
}
}

// Descriptor access: Client is the raw client (or ClientWrapper, both expose
// getParameterDescriptors()).
template <typename Client>
constexpr auto& descriptors()
{
  return Client::getParameterDescriptors();
}

template <typename Client, std::size_t N>
using descriptor_t = std::decay_t<
    decltype(descriptors<Client>().template get<N>())>;

// Compile-time parameter index lookup by flucoma name (the first argument of
// FloatParam/LongParam/...). Misspelled names fail to compile. Essential for
// NRT clients, whose parameter sets are composed by makeNRTParams and don't
// match the RT enum indices.
template <typename Client>
consteval std::size_t pidx(std::string_view name)
{
  constexpr auto& descs = descriptors<Client>();
  constexpr std::size_t n = descs.size();
  std::size_t res = ~std::size_t(0);
  [&]<std::size_t... Is>(std::index_sequence<Is...>) {
    ((void)(std::string_view(descs.template get<Is>().name) == name
            && (res = Is, true)),
     ...);
  }(std::make_index_sequence<n>{});
  if(res == ~std::size_t(0))
    throw "parameter name not found in this client";
  return res;
}

// min / max bounds of parameter N, from its Min()/Max() constraints if present,
// otherwise a widened heuristic around the default value.
template <typename Client, std::size_t N>
constexpr double param_min()
{
  constexpr auto& descs = descriptors<Client>();
  const auto& constraints = std::get<1>(std::get<N>(descs.descriptors()));
  constexpr std::size_t n
      = std::tuple_size_v<std::decay_t<decltype(constraints)>>;
  if(auto b = detail::find_bound_impl<fluid::client::impl::MinImpl>(
         constraints, std::make_index_sequence<n>{}))
    return *b;
  return 0.;
}

template <typename Client, std::size_t N>
constexpr double param_max()
{
  constexpr auto& descs = descriptors<Client>();
  const auto& constraints = std::get<1>(std::get<N>(descs.descriptors()));
  constexpr std::size_t n
      = std::tuple_size_v<std::decay_t<decltype(constraints)>>;
  if(auto b = detail::find_bound_impl<fluid::client::impl::MaxImpl>(
         constraints, std::make_index_sequence<n>{}))
    return *b;

  // No explicit max: pick something usable in a UI.
  constexpr auto& d = descriptors<Client>().template get<N>();
  const double def = static_cast<double>(d.defaultValue);
  return def > 0. ? def * 4. : 100.;
}

// --- control ports ---------------------------------------------------------

// Float parameter -> slider
template <typename Client, std::size_t N>
struct fluid_float_param
{
  static constexpr auto& desc = descriptors<Client>().template get<N>();
  static constexpr std::size_t fluid_index = N;

  static consteval auto name() { return std::string_view{desc.displayName}; }

  enum widget
  {
    hslider
  };

  struct range
  {
    const double min = param_min<Client, N>();
    const double max = param_max<Client, N>();
    const double init = static_cast<double>(desc.defaultValue);
  };

  double value = static_cast<double>(desc.defaultValue);
  operator double&() noexcept { return value; }
  operator const double&() const noexcept { return value; }
};

// Long (integer) parameter -> spinbox
template <typename Client, std::size_t N>
struct fluid_long_param
{
  static constexpr auto& desc = descriptors<Client>().template get<N>();
  static constexpr std::size_t fluid_index = N;

  static consteval auto name() { return std::string_view{desc.displayName}; }

  enum widget
  {
    spinbox
  };

  struct range
  {
    const int min = static_cast<int>(param_min<Client, N>());
    const int max = static_cast<int>(param_max<Client, N>());
    const int init = static_cast<int>(desc.defaultValue);
  };

  int value = static_cast<int>(desc.defaultValue);
  operator int&() noexcept { return value; }
  operator const int&() const noexcept { return value; }
};

// Enum parameter -> combobox over the descriptor's strings
template <typename Client, std::size_t N>
struct fluid_enum_param
{
  static constexpr auto& desc = descriptors<Client>().template get<N>();
  static constexpr std::size_t fluid_index = N;
  static constexpr std::size_t num_options
      = static_cast<std::size_t>(desc.numOptions);

  static consteval auto name() { return std::string_view{desc.displayName}; }

  enum widget
  {
    combobox
  };

  static constexpr auto make_values()
  {
    std::array<std::string_view, num_options> res{};
    for(std::size_t i = 0; i < num_options; ++i)
      res[i] = desc.strings[i];
    return res;
  }

  struct range
  {
    std::array<std::string_view, num_options> values = make_values();
    int init = static_cast<int>(desc.defaultValue);
  };

  int value = static_cast<int>(desc.defaultValue);
  operator int&() noexcept { return value; }
  operator const int&() const noexcept { return value; }
};

// FFT settings (FFTParamsT): exposed as three integer ports.
// role: 0 = window size, 1 = hop size (-1: window/2), 2 = fft size (-1: window)
// FFTParams' accessors are not constexpr, so the defaults are template
// parameters; every flucoma client to date uses FFTParam(..., 1024, -1, -1).
template <typename Client, std::size_t N, int Role, int Default>
struct fluid_fft_param
{
  static constexpr int fft_role = Role;
  static constexpr std::size_t fluid_index = N;

  static consteval auto name()
  {
    if constexpr(Role == 0)
      return std::string_view{"Window Size"};
    else if constexpr(Role == 1)
      return std::string_view{"Hop Size"};
    else
      return std::string_view{"FFT Size"};
  }

  enum widget
  {
    spinbox
  };

  struct range
  {
    const int min = Role == 0 ? 4 : -1;
    const int max = 65536;
    const int init = Default;
  };

  int value = Default;
  operator int&() noexcept { return value; }
  operator const int&() const noexcept { return value; }
};

template <typename Client, std::size_t N, int Default = 1024>
using fluid_fft_window = fluid_fft_param<Client, N, 0, Default>;
template <typename Client, std::size_t N, int Default = -1>
using fluid_fft_hop = fluid_fft_param<Client, N, 1, Default>;
template <typename Client, std::size_t N, int Default = -1>
using fluid_fft_size = fluid_fft_param<Client, N, 2, Default>;

// LongRuntimeMaxT (e.g. numCoeffs/numBands): int spinbox. Like FFTParams, its
// accessors are not constexpr, so the default comes in as a template argument
// (copy it from the client's defineParameters call).
template <typename Client, std::size_t N, int Default, int Max = 256>
struct fluid_long_max_param
{
  static constexpr auto& desc = descriptors<Client>().template get<N>();
  static constexpr std::size_t fluid_index = N;

  static consteval auto name() { return std::string_view{desc.displayName}; }

  enum widget
  {
    spinbox
  };

  struct range
  {
    const int min = 1;
    const int max = Max;
    const int init = Default;
  };

  int value = Default;
  operator int&() noexcept { return value; }
  operator const int&() const noexcept { return value; }
};

// Float parameter with an explicit UI range, for params whose constraints
// don't yield a usable one (e.g. "maxFreq" with default -1 meaning auto).
template <typename Client, std::size_t N, double Min, double Max, double Init>
struct fluid_float_param_ranged
{
  static constexpr auto& desc = descriptors<Client>().template get<N>();
  static constexpr std::size_t fluid_index = N;

  static consteval auto name() { return std::string_view{desc.displayName}; }

  enum widget
  {
    hslider
  };

  struct range
  {
    const double min = Min;
    const double max = Max;
    const double init = Init;
  };

  double value = Init;
  operator double&() noexcept { return value; }
  operator const double&() const noexcept { return value; }
};

template <typename Client, std::size_t N, int Min, int Max, int Init>
struct fluid_long_param_ranged
{
  static constexpr auto& desc = descriptors<Client>().template get<N>();
  static constexpr std::size_t fluid_index = N;

  static consteval auto name() { return std::string_view{desc.displayName}; }

  enum widget
  {
    spinbox
  };

  struct range
  {
    const int min = Min;
    const int max = Max;
    const int init = Init;
  };

  int value = Init;
  operator int&() noexcept { return value; }
  operator const int&() const noexcept { return value; }
};

// "Auto" toggle companion for params whose -1 sentinel means "automatic"
// (Nyquist, source length, ...). When on, the associated parameter is set to
// -1 and the slider's value is ignored — same idiom as the LFO's
// quantification selector overriding its frequency knob.
template <typename Client, std::size_t N, bool Init = true>
struct fluid_auto_toggle
{
  static constexpr auto& desc = descriptors<Client>().template get<N>();
  static constexpr std::size_t fluid_auto_index = N;

  static consteval auto name()
  {
    return std::string_view{"Auto"};
  }

  enum widget
  {
    toggle
  };

  struct range
  {
    const bool min = false;
    const bool max = true;
    const bool init = Init;
  };

  bool value = Init;
  operator bool&() noexcept { return value; }
  operator const bool&() const noexcept { return value; }
};

// Pick the right port for descriptor type of parameter N.
template <typename Client, std::size_t N>
using fluid_param_for = std::conditional_t<
    std::is_same_v<descriptor_t<Client, N>, fluid::client::FloatT>,
    fluid_float_param<Client, N>,
    std::conditional_t<
        std::is_same_v<descriptor_t<Client, N>, fluid::client::LongT>,
        fluid_long_param<Client, N>,
        std::conditional_t<
            std::is_same_v<descriptor_t<Client, N>, fluid::client::EnumT>,
            fluid_enum_param<Client, N>, void>>>;

// --- parameter sync ---------------------------------------------------------

// Copy current avnd port values into a FluCoMa parameter set. Ins is the avnd
// inputs aggregate; every member carrying fluid_index / fft_role /
// fluid_auto_index participates. Shared by the RT and NRT hosts.
template <typename ParamSetType, typename Ins>
void sync_params_into(ParamSetType& params, Ins& ins)
{
  // First pass: gather "Auto" toggles — an enabled toggle forces its
  // parameter to the -1 sentinel regardless of the slider value.
  constexpr std::size_t param_count = std::tuple_size_v<
      std::decay_t<decltype(params.constrainParameterValues())>>;
  std::array<bool, param_count> auto_on{};
  boost::pfr::for_each_field(ins, [&]<typename Field>(Field& field) {
    if constexpr(requires { Field::fluid_auto_index; })
      auto_on[Field::fluid_auto_index] = static_cast<bool>(field.value);
  });

  boost::pfr::for_each_field(ins, [&]<typename Field>(Field& field) {
    if constexpr(requires { Field::fft_role; })
    {
      // FFT sub-port: mutate the corresponding component of the FFTParams
      // value in place; the FFT constraint re-normalizes afterwards.
      constexpr std::size_t N = Field::fluid_index;
      auto& fft = params.template get<N>();
      if constexpr(Field::fft_role == 0)
        fft.setWin(field.value);
      else if constexpr(Field::fft_role == 1)
        fft.setHop(field.value);
      else
        fft.setFFT(field.value);
    }
    else if constexpr(requires { Field::fluid_index; } && requires { field.value; })
    {
      constexpr std::size_t N = Field::fluid_index;
      using T = typename std::decay_t<
          decltype(std::declval<ParamSetType&>().template get<N>())>;
      if(auto_on[N])
        params.template set<N>(static_cast<T>(-1), nullptr);
      else
        params.template set<N>(static_cast<T>(field.value), nullptr);
    }
  });
  params.constrainParameterValues();
}

// --- RT host ----------------------------------------------------------------

/* Engine member for realtime FluCoMa clients wrapped as avnd objects.
 *
 *   using Client = fluid::client::RTAmpSliceClient; // a ClientWrapper<...>
 *   FluCoMa::FluidRTHost<Client> host;
 *
 * The owning avnd object calls host.prepare(rate, frames) from prepare() and
 * host.process(inputs, in_channels, out_channels, frames) from operator().
 */
template <typename Client>
struct FluidRTHost
{
  using ParamDescType = typename Client::ParamDescType;
  using ParamSetType = fluid::client::ParameterSet<ParamDescType>;

  ParamSetType params;
  fluid::client::FluidContext context;
  Client client;

  FluidRTHost()
      : params{Client::getParameterDescriptors(), fluid::FluidDefaultAllocator()}
      , client{params, context}
  {
  }

  FluidRTHost(FluidRTHost&& other) noexcept
      : params{std::move(other.params)}
      , context{other.context}
      , client{std::move(other.client)}
  {
    // ClientWrapper's move keeps referencing the old ParameterSet
    client.setParams(params);
  }

  FluidRTHost(const FluidRTHost&) = delete;
  FluidRTHost& operator=(const FluidRTHost&) = delete;
  FluidRTHost& operator=(FluidRTHost&&) = delete;

  void prepare(double sample_rate, int buffer_frames)
  {
    client.sampleRate(sample_rate);
    context.hostVectorSize(buffer_frames);
    // Probe the wrapped client, not ClientWrapper: its reset(FluidContext&)
    // is declared unconditionally and fails to instantiate for clients whose
    // reset takes no argument (e.g. GainClient).
    using Raw = typename Client::Client;
    if constexpr(requires(Raw& r) { r.reset(context); })
      client.reset(context);
  }

  // Copy current avnd port values into the FluCoMa parameter set.
  // Ins is the avnd inputs aggregate; every member created from
  // fluid_param_for<Client, N> is synced to parameter N.
  template <typename Ins>
  void sync_params(Ins& ins)
  {
    sync_params_into(params, ins);
  }

  // Some clients size internal state from parameter maxima at construction
  // (e.g. NMFFilter's output rank): call after sync_params in prepare() so
  // construction sees the actual values.
  void reconstruct() { client = Client{params, context}; }

  // For RT clients with buffer parameters (NMF bases etc.): set from a
  // loaded buffer; persists across ticks (sync_params does not touch it).
  template <std::size_t N, typename Buf>
  void set_input_buffer(Buf buf)
  {
    params.template set<N>(
        fluid::client::InputBufferUnderlyingType(std::move(buf)), nullptr);
  }

  // Run one buffer through the client. in/out are arrays of channel pointers.
  void process(double** in, int in_channels, double** out, int out_channels,
               int frames)
  {
    std::vector<fluid::FluidTensorView<double, 1>> ins, outs;
    ins.reserve(in_channels);
    outs.reserve(out_channels);
    for(int c = 0; c < in_channels; ++c)
      ins.emplace_back(in[c], 0, frames);
    for(int c = 0; c < out_channels; ++c)
      outs.emplace_back(out[c], 0, frames);

    context.hostVectorSize(frames);
    client.process(ins, outs, context);
  }

  // For AudioIn + ControlOut clients (Pitch, Loudness, MFCC, ...): run one
  // buffer, write the latest feature vector into features[0..n_features).
  void process_control(double** in, int in_channels, int frames,
                       double* features, int n_features)
  {
    std::vector<fluid::FluidTensorView<double, 1>> ins, outs;
    ins.reserve(in_channels);
    for(int c = 0; c < in_channels; ++c)
      ins.emplace_back(in[c], 0, frames);
    outs.emplace_back(features, 0, n_features);

    context.hostVectorSize(frames);
    client.process(ins, outs, context);
  }

};

}
