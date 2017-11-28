#include <cmath>
#include "sav/export.hpp"
#include "sav/utility.hpp"
#include "savvy/vcf_reader.hpp"
#include "savvy/sav_reader.hpp"
#include "savvy/savvy.hpp"

#include <set>
#include <fstream>
#include <ctime>
#include <getopt.h>

class export_prog_args
{
private:
  static const int default_compression_level = 3;
  static const int default_block_size = 2048;

  std::vector<option> long_options_;
  std::set<std::string> subset_ids_;
  std::vector<savvy::region> regions_;
  std::string input_path_;
  std::string output_path_;
  bool help_ = false;
  savvy::fmt format_ = savvy::fmt::allele;
public:
  export_prog_args() :
    long_options_(
      {
        {"format", required_argument, 0, 'f'},
        {"help", no_argument, 0, 'h'},
        {"regions", required_argument, 0, 'r'},
        {"samples", required_argument, 0, 's'},
        {"samples-file", required_argument, 0, 'S'},
        {0, 0, 0, 0}
      })
  {
  }

  const std::string& input_path() const { return input_path_; }
  const std::string& output_path() const { return output_path_; }
  const std::set<std::string>& subset_ids() const { return subset_ids_; }
  const std::vector<savvy::region>& regions() const { return regions_; }
  savvy::fmt format() const { return format_; }
  bool help_is_set() const { return help_; }

  void print_usage(std::ostream& os)
  {
    os << "----------------------------------------------\n";
    os << "Usage: sav export [opts] [in.sav] [out.{vcf,vcf.gz,bcf}]\n";
    os << "\n";
    //os << " -f, --format     : Format field to copy (GT, DS or HDS, default: GT)\n";
    os << " -h, --help         : Print usage\n";
    os << " -r, --regions      : Comma separated list of regions formated as chr[:start-end]\n";
    os << " -s, --samples      : Comma separated list of sample IDs to subset\n";
    os << " -S, --samples-file : Path to file containing list of sample IDs to subset\n";
    os << "----------------------------------------------\n";
    os << std::flush;
  }

  bool parse(int argc, char** argv)
  {
    int long_index = 0;
    int opt = 0;
    while ((opt = getopt_long(argc, argv, "f:hr:s:S:", long_options_.data(), &long_index )) != -1)
    {
      char copt = char(opt & 0xFF);
      switch (copt)
      {
        case 'f':
        {
          std::string str_opt_arg(optarg ? optarg : "");
          if (str_opt_arg == "HDS")
          {
            format_ = savvy::fmt::haplotype_dosage;
          }
          else if (str_opt_arg == "DS")
          {
            format_ = savvy::fmt::dosage;
          }
//          else if (str_opt_arg == "GP")
//          {
//            format_ = savvy::fmt::genotype_probability;
//          }
          else if (str_opt_arg != "GT")
          {
            std::cerr << "Invalid format field value (" << str_opt_arg << ")\n";
            return false;
          }
          break;
        }
        case 'h':
          help_ = true;
          break;
        case 'r':
          for (const auto& r : split_string_to_vector(optarg, ','))
            regions_.emplace_back(string_to_region(r));
          break;
        case 's':
          subset_ids_ = split_string_to_set(optarg, ',');
          break;
        case 'S':
          subset_ids_ = split_file_to_set(optarg);
          break;
        default:
          return false;
      }
    }

    int remaining_arg_count = argc - optind;

    if (remaining_arg_count == 0)
    {
      if (regions_.size())
      {
        std::cerr << "Input path must be specified when using --regions option." << std::endl;
        return false;
      }

      input_path_ = "/dev/stdin";
      output_path_ = "/dev/stdout";
    }
    else if (remaining_arg_count == 1)
    {
      input_path_ = argv[optind];
      output_path_ = "/dev/stdout";
    }
    else if (remaining_arg_count == 2)
    {
      input_path_ = argv[optind];
      output_path_ = argv[optind + 1];
    }
    else
    {
      std::cerr << "Too many arguments\n";
      return false;
    }

    return true;
  }
};

int export_records(savvy::sav::reader& in, const std::vector<savvy::region>& regions, savvy::vcf::writer<1>& out)
{
  savvy::site_info variant;
  std::vector<float> genotypes;

  while (in.read(variant, genotypes))
    out.write(variant, genotypes);

  return out.good() ? EXIT_SUCCESS : EXIT_FAILURE;
}

int export_records(savvy::sav::indexed_reader& in, const std::vector<savvy::region>& regions, savvy::vcf::writer<1>& out)
{
  savvy::site_info variant;
  std::vector<float> genotypes;

  while (in.read(variant, genotypes))
    out.write(variant, genotypes);

  if (regions.size())
  {
    for (auto it = regions.begin() + 1; it != regions.end(); ++it)
    {
      in.reset_region(*it);
      while (in.read(variant, genotypes))
        out.write(variant, genotypes);
    }
  }

  return out.good() ? EXIT_SUCCESS : EXIT_FAILURE;
}

template <typename T>
int export_reader(T& input, const export_prog_args& args)
{
  std::vector<std::string> sample_ids(input.samples_end() - input.samples_begin());
  std::copy(input.samples_begin(), input.samples_end(), sample_ids.begin());
  if (args.subset_ids().size())
    sample_ids = input.subset_samples(args.subset_ids());

  auto variant_metadata = input.prop_fields();

  auto headers = input.headers();

  for (auto it = headers.begin(); it != headers.end(); )
  {
    std::string header_id = savvy::parse_header_id(it->second);
    if ((it->first == "INFO" && (header_id == "ID" || header_id == "QUAL" || header_id == "FILTER")) || it->first == "FORMAT")
      it = headers.erase(it);
    else
    {
      if (it->first == "fileDate")
      {
        std::time_t t = std::time(nullptr);
        char datestr[11];
        if (std::strftime(datestr, sizeof(datestr), "%Y%m%d", std::localtime(&t)))
        {
          it->second = std::string(datestr);
        }
      }

      ++it;
    }
  }

  if (args.format() == savvy::fmt::allele)
    headers.emplace_back("FORMAT", "<ID=GT,Number=1,Type=String,Description=\"Genotype\">");
  else if (args.format() == savvy::fmt::haplotype_dosage)
    headers.emplace_back("FORMAT", "<ID=HDS,Number=2,Type=Float,Description=\"Estimated Haploid Alternate Allele Dosage\">");
  else if (args.format() == savvy::fmt::dosage)
    headers.emplace_back("FORMAT", "<ID=DS,Number=1,Type=Float,Description=\"Estimated Alternate Allele Dosage\">");
//  else if (args.format() == savvy::fmt::genotype_probability)
//    headers.emplace_back("FORMAT", "<ID=GP,Number=3,Type=Float,Description=\"Estimated Posterior Probabilities for Genotypes 0/0, 0/1 and 1/1\">"); // TODO: Handle other ploidy levels.

  savvy::vcf::writer<1> vcf_output(args.output_path(), sample_ids.begin(), sample_ids.end(), headers.begin(), headers.end(), args.format());
  return export_records(input, args.regions(), vcf_output);
}

int export_main(int argc, char** argv)
{
  export_prog_args args;
  if (!args.parse(argc, argv))
  {
    args.print_usage(std::cerr);
    return EXIT_FAILURE;
  }

  if (args.help_is_set())
  {
    args.print_usage(std::cout);
    return EXIT_SUCCESS;
  }

  if (args.regions().size())
  {
    savvy::sav::indexed_reader input(args.input_path(), args.regions().front(), args.format());
    return export_reader(input, args);
  }
  else
  {
    savvy::sav::reader input(args.input_path(), args.format());
    return export_reader(input, args);
  }
}