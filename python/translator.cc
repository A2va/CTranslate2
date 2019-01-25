#include <boost/python.hpp>
#include <boost/python/stl_iterator.hpp>

#include <ctranslate2/translator_pool.h>
#include <ctranslate2/utils.h>

namespace py = boost::python;

class GILReleaser {
public:
  GILReleaser()
    : _save_state(PyEval_SaveThread()) {
  }
  ~GILReleaser() {
    PyEval_RestoreThread(_save_state);
  }
private:
  PyThreadState* _save_state;
};

template<class T>
py::list std_vector_to_py_list(const std::vector<T>& v) {
  py::list l;
  for (const auto& x : v)
    l.append(x);
  return l;
}

static void initialize(size_t mkl_num_threads) {
  ctranslate2::initialize(mkl_num_threads);
}

class TranslatorWrapper
{
public:
  TranslatorWrapper(const std::string& model_path,
                    const std::string& device,
                    int device_index,
                    size_t thread_pool_size)
    : _translator_pool(thread_pool_size,
                       ctranslate2::models::ModelFactory::load(model_path,
                                                               ctranslate2::str_to_device(device),
                                                               device_index)) {
  }

  void translate_file(const std::string& in_file,
                      const std::string& out_file,
                      size_t max_batch_size,
                      size_t beam_size,
                      size_t num_hypotheses,
                      float length_penalty,
                      size_t max_decoding_length,
                      size_t min_decoding_length,
                      bool use_vmap,
                      bool with_scores) {
    auto options = ctranslate2::TranslationOptions();
    options.beam_size = beam_size;
    options.length_penalty = length_penalty;
    options.max_decoding_length = max_decoding_length;
    options.min_decoding_length = min_decoding_length;
    options.num_hypotheses = num_hypotheses;
    options.use_vmap = use_vmap;

    GILReleaser releaser;
    _translator_pool.consume_text_file(in_file, out_file, max_batch_size, options, with_scores);
  }

  py::list translate_batch(const py::object& tokens,
                           size_t beam_size,
                           size_t num_hypotheses,
                           float length_penalty,
                           size_t max_decoding_length,
                           size_t min_decoding_length,
                           bool use_vmap,
                           bool return_attention) {
    if (tokens == py::object())
      return py::list();

    std::vector<std::vector<std::string>> tokens_vec;
    tokens_vec.reserve(py::len(tokens));

    for (auto it = py::stl_input_iterator<py::list>(tokens);
         it != py::stl_input_iterator<py::list>(); it++) {
      tokens_vec.emplace_back(py::stl_input_iterator<std::string>(*it),
                              py::stl_input_iterator<std::string>());
    }

    auto options = ctranslate2::TranslationOptions();
    options.beam_size = beam_size;
    options.length_penalty = length_penalty;
    options.max_decoding_length = max_decoding_length;
    options.min_decoding_length = min_decoding_length;
    options.num_hypotheses = num_hypotheses;
    options.use_vmap = use_vmap;
    options.return_attention = return_attention;

    std::vector<ctranslate2::TranslationResult> results;

    {
      GILReleaser releaser;
      results = std::move(_translator_pool.post(tokens_vec, options).get());
    }

    py::list py_results;
    for (const auto& result : results) {
      py::list batch;
      for (size_t i = 0; i < result.num_hypotheses(); ++i) {
        py::dict hyp;
        hyp["score"] = result.scores()[i];
        hyp["tokens"] = std_vector_to_py_list(result.hypotheses()[i]);
        if (result.has_attention()) {
          py::list attn;
          for (const auto& attn_vector : result.attention()[i])
            attn.append(std_vector_to_py_list(attn_vector));
          hyp["attention"] = attn;
        }
        batch.append(hyp);
      }
      py_results.append(batch);
    }

    return py_results;
  }

private:
  ctranslate2::TranslatorPool _translator_pool;
};

BOOST_PYTHON_MODULE(translator)
{
  PyEval_InitThreads();
  py::def("initialize", initialize, (py::arg("mkl_num_threads")=4));
  py::class_<TranslatorWrapper, boost::noncopyable>(
    "Translator",
    py::init<std::string, std::string, int, size_t>(
      (py::arg("model_path"),
       py::arg("device")="cpu",
       py::arg("device_index")=0,
       py::arg("thread_pool_size")=1)))
    .def("translate_batch", &TranslatorWrapper::translate_batch,
         (py::arg("tokens"),
          py::arg("beam_size")=4,
          py::arg("num_hypotheses")=1,
          py::arg("length_penalty")=0.6,
          py::arg("max_decoding_length")=250,
          py::arg("min_decoding_length")=1,
          py::arg("use_vmap")=false,
          py::arg("return_attention")=false))
    .def("translate_file", &TranslatorWrapper::translate_file,
         (py::arg("input_path"),
          py::arg("output_path"),
          py::arg("max_batch_size"),
          py::arg("beam_size")=4,
          py::arg("num_hypotheses")=1,
          py::arg("length_penalty")=0.6,
          py::arg("max_decoding_length")=250,
          py::arg("min_decoding_length")=1,
          py::arg("use_vmap")=false,
          py::arg("with_scores")=false))
    ;
}