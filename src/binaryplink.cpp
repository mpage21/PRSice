// This file is part of PRSice2.0, copyright (C) 2016-2017
// Shing Wan Choi, Jack Euesden, Cathryn M. Lewis, Paul F. O’Reilly
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "binaryplink.hpp"

BinaryPlink::BinaryPlink(const Commander& commander, Reporter& reporter,
                         bool ld, bool verbose)
{
    // currently filtering is univerisal (same filter apply to
    // target and ld reference)
    set_info(commander);
    const bool ignore_fid = commander.ignore_fid();
    std::string out_prefix = commander.out();

    std::string prefix = (ld) ? commander.ld_prefix() : commander.target_name();
    // check if there is an external fam file
    std::vector<std::string> token;
    token = misc::split(prefix, ",");
    std::string fam = "";
    std::string bfile_prefix = prefix;
    if (token.size() == 2) {
        fam = token[1];
        bfile_prefix = token[0];
    }
    std::string message = "Loading Genotype file: " + bfile_prefix + " (bed)\n";
    if (!fam.empty()) {
        message.append("With external fam file: " + fam + "\n");
    }
    reporter.report(message);


    m_nonfounder = commander.nonfounders();
    m_fam_name = fam;
    /** simple assignments **/
    filter.keep_ambig = commander.keep_ambig();
    m_thread = commander.thread();
    // get the exclusion and extraction list
    if (!commander.remove_sample_file().empty()) {
        m_sample_selection_list =
            load_ref(commander.remove_sample_file(), ignore_fid);
    }
    if (!commander.keep_sample_file().empty()) {
        m_remove_sample = false;
        m_sample_selection_list =
            load_ref(commander.keep_sample_file(), ignore_fid);
    }
    if (!commander.extract_snp_file().empty()) {
        m_exclude_snp = false;
        m_snp_selection_list =
            load_snp_list(commander.extract_snp_file(), reporter);
    }
    if (!commander.exclude_snp_file().empty()) {
        m_snp_selection_list =
            load_snp_list(commander.exclude_snp_file(), reporter);
    }


    /** setting the chromosome information **/
    m_xymt_codes.resize(XYMT_OFFSET_CT);
    // we are not using the following script for now as we only support human
    m_haploid_mask.resize(CHROM_MASK_WORDS, 0);
    m_chrom_mask.resize(CHROM_MASK_WORDS, 0);

    // now initialize the chromosome
    init_chr(commander.num_auto(), commander.no_x(), commander.no_y(),
             commander.no_xy(), commander.no_mt());


    /** now get the chromosome information we've got by replacing the # in the
     * name **/
    // if there are multiple #, they will all be replaced by the same number
    set_genotype_files(bfile_prefix);


    /** now read the sample information **/
    if (ld) {
        // we don't need sample information when this is the
        // ld file, only need the binary arrays which is
        // initialized and set by the load_sample function
        load_samples(ignore_fid);
    }
    else
    {
        m_sample_names = load_samples(ignore_fid);
    }
    /** now read the SNP information **/
    // to save memory, we would like to reduce the
    // number of SNP information stored for the ld reference
    // as all we need is only the snp id and location
    m_existed_snps = load_snps(out_prefix);
    m_marker_ct = m_existed_snps.size();

    if (verbose) {
        std::string message = std::to_string(m_unfiltered_sample_ct)
                              + " people (" + std::to_string(m_num_male)
                              + " male(s), " + std::to_string(m_num_female)
                              + " female(s)) observed\n";
        message.append(std::to_string(m_founder_ct) + " founder(s) included\n");
        if (m_num_ambig != 0 && !filter.keep_ambig) {
            message.append(std::to_string(m_num_ambig)
                           + " ambiguous variant(s) excluded\n");
        }
        else if (m_num_ambig != 0)
        {
            message.append(std::to_string(m_num_ambig)
                           + " ambiguous variant(s) kept\n");
        }
        message.append(std::to_string(m_marker_ct) + " variant(s) included\n");
        reporter.report(message);
    }

    check_bed();
    // MAF filtering should be performed here
    // don't bother doing this if user doesn't want to
    if (filter.filter_geno || filter.filter_maf) snp_filtering(reporter);
    m_cur_file = "";

    uintptr_t unfiltered_sample_ctl = BITCT_TO_WORDCT(m_unfiltered_sample_ct);
    m_tmp_genotype.resize(unfiltered_sample_ctl * 2, 0);
    m_sample_selection_list.clear();
    m_snp_selection_list.clear();
}


BinaryPlink::BinaryPlink(std::string prefix, std::string remove_sample,
                         std::string keep_sample, std::string extract_snp,
                         std::string exclude_snp, std::string fam_name,
                         const std::string& out_prefix, Reporter& reporter,
                         bool ignore_fid, bool nonfounder, int num_auto,
                         bool no_x, bool no_y, bool no_xy, bool no_mt,
                         bool keep_ambig, const size_t thread, bool verbose)
{
    m_nonfounder = nonfounder;
    m_fam_name = fam_name;
    /** simple assignments **/
    filter.keep_ambig = keep_ambig;
    m_thread = thread;
    // get the exclusion and extraction list
    if (!remove_sample.empty()) {
        m_sample_selection_list = load_ref(remove_sample, ignore_fid);
    }
    if (!keep_sample.empty()) {
        m_remove_sample = false;
        m_sample_selection_list = load_ref(keep_sample, ignore_fid);
    }
    if (!extract_snp.empty()) {
        m_exclude_snp = false;
        m_snp_selection_list = load_snp_list(extract_snp, reporter);
    }
    if (!exclude_snp.empty()) {
        m_snp_selection_list = load_snp_list(exclude_snp, reporter);
    }


    /** setting the chromosome information **/
    m_xymt_codes.resize(XYMT_OFFSET_CT);
    // we are not using the following script for now as we only support human
    m_haploid_mask.resize(CHROM_MASK_WORDS, 0);
    m_chrom_mask.resize(CHROM_MASK_WORDS, 0);

    // now initialize the chromosome
    init_chr(num_auto, no_x, no_y, no_xy, no_mt);


    /** now get the chromosome information we've got by replacing the # in the
     * name **/
    // if there are multiple #, they will all be replaced by the same number
    set_genotype_files(prefix);


    /** now read the sample information **/
    m_sample_names = load_samples(ignore_fid);

    /** now read the SNP information **/
    m_existed_snps = load_snps(out_prefix);
    m_marker_ct = m_existed_snps.size();

    if (verbose) {
        std::string message = std::to_string(m_unfiltered_sample_ct)
                              + " people (" + std::to_string(m_num_male)
                              + " male(s), " + std::to_string(m_num_female)
                              + " female(s)) observed\n";
        message.append(std::to_string(m_founder_ct) + " founder(s) included\n");
        if (m_num_ambig != 0 && !keep_ambig) {
            message.append(std::to_string(m_num_ambig)
                           + " ambiguous variant(s) excluded\n");
        }
        else if (m_num_ambig != 0)
        {
            message.append(std::to_string(m_num_ambig)
                           + " ambiguous variant(s) kept\n");
        }
        message.append(std::to_string(m_marker_ct) + " variant(s) included\n");
        reporter.report(message);
    }

    check_bed();
    m_cur_file = "";

    uintptr_t unfiltered_sample_ctl = BITCT_TO_WORDCT(m_unfiltered_sample_ct);
    m_tmp_genotype.resize(unfiltered_sample_ctl * 2, 0);
    m_sample_selection_list.clear();
    m_snp_selection_list.clear();
}

BinaryPlink::~BinaryPlink() {}


std::vector<Sample> BinaryPlink::load_samples(bool ignore_fid)
{
    assert(m_genotype_files.size() > 0);
    // get the name of the first fam file (we only need the first as they should
    // all contain the same information)
    std::string famName = "";
    if (!m_fam_name.empty())
        famName = m_fam_name;
    else
        famName = m_genotype_files.front() + ".fam";
    // open the fam file
    std::ifstream famfile;
    famfile.open(famName.c_str());
    if (!famfile.is_open()) {
        std::string error_message = "ERROR: Cannot open fam file: " + famName;
        throw std::runtime_error(error_message);
    }
    // number of unfiltered samples
    m_unfiltered_sample_ct = 0;
    std::string line;
    std::unordered_set<std::string> founder_info;
    // first pass to get the number of samples and also get the founder ID
    while (std::getline(famfile, line)) {
        misc::trim(line);
        if (!line.empty()) {
            std::vector<std::string> token = misc::split(line);
            if (token.size() < 6) {
                std::string message =
                    "Error: Malformed fam file. Less than 6 column on "
                    "line: "
                    + std::to_string(m_unfiltered_sample_ct + 1) + "\n";
                throw std::runtime_error(message);
            }
            founder_info.insert(token[+FAM::FID] + "_" + token[+FAM::IID]);
            m_unfiltered_sample_ct++;
        }
    }
    // now reset the fam file to the start
    famfile.clear();
    famfile.seekg(0);

    uintptr_t unfiltered_sample_ctl = BITCT_TO_WORDCT(m_unfiltered_sample_ct);

    // we don't work with the sex for now, so better ignore them first
    // m_sex_male = new uintptr_t[unfiltered_sample_ctl];
    // std::fill(m_sex_male, m_sex_male+unfiltered_sample_ctl, 0);
    // std::memset(m_sex_male, 0x0, unfiltered_sample_ctl*sizeof(uintptr_t));

    // try to use fill instead of memset for better readability (will be tiny
    // bit slower according to stackoverflow)
    m_founder_info.resize(unfiltered_sample_ctl, 0);

    // Initialize this, but will copy founder into this later on
    m_sample_include.resize(unfiltered_sample_ctl, 0);

    m_num_male = 0, m_num_female = 0, m_num_ambig_sex = 0,
    m_num_non_founder = 0;
    std::vector<Sample> sample_name;
    uintptr_t sample_uidx = 0; // this is just for error message
    while (std::getline(famfile, line)) {
        misc::trim(line);
        if (line.empty()) continue;
        std::vector<std::string> token = misc::split(line);
        if (token.size() < 6) {
            std::string error_message =
                "Error: Malformed fam file. Less than 6 column on line: "
                + std::to_string(sample_uidx + 1);
            throw std::runtime_error(error_message);
        }
        Sample cur_sample;
        cur_sample.FID = token[+FAM::FID];
        cur_sample.IID = token[+FAM::IID];
        std::string id = (ignore_fid)
                             ? token[+FAM::IID]
                             : token[+FAM::FID] + "_" + token[+FAM::IID];
        cur_sample.pheno = token[+FAM::PHENOTYPE];
        cur_sample.has_pheno =
            false; // only true when we have evaluated it to be true
        if (!m_remove_sample) {
            cur_sample.included = (m_sample_selection_list.find(id)
                                   != m_sample_selection_list.end());
        }
        else
        {
            cur_sample.included = (m_sample_selection_list.find(id)
                                   == m_sample_selection_list.end());
        }


        cur_sample.num_snp = 0;

        if (founder_info.find(token[+FAM::FATHER]) == founder_info.end()
            && founder_info.find(token[+FAM::MOTHER]) == founder_info.end()
            && cur_sample.included)
        {
            // only set this if no parents were found in the fam file
            m_founder_ct++;
            SET_BIT(sample_uidx, m_founder_info.data()); // essentially,
                                                         // m_founder is a
                                                         // subset of
                                                         // m_sample_include
        }
        else if (cur_sample.included && m_nonfounder)
        {
            // nonfounder but we want to keep it
            SET_BIT(sample_uidx, m_sample_include.data());
            m_num_non_founder++;
        }
        else
        {
            // nonfounder / unwanted sample
            if (cur_sample.included) {
                // user didn't specify they want the nonfounder
                // so ignore it
                cur_sample.included = false;
                m_num_non_founder++;
            }
        }
        m_sample_ct += cur_sample.included;
        if (cur_sample.included) SET_BIT(sample_uidx, m_sample_include.data());
        if (token[+FAM::SEX].compare("1") == 0) {
            m_num_male++;
            // SET_BIT(sample_uidx, m_sex_male); // if that individual is male,
            // need to set bit
        }
        else if (token[+FAM::SEX].compare("2") == 0)
        {
            m_num_female++;
        }
        else
        {
            m_num_ambig_sex++; // currently ignore as we don't do sex chromosome
            // SET_BIT(sample_uidx, m_sample_exclude); // exclude any samples
            // without sex information
        }
        sample_uidx++;
        sample_name.push_back(cur_sample);
    }
    famfile.close();
    return sample_name;
}

std::vector<SNP> BinaryPlink::load_snps(const std::string& out_prefix)
{
    assert(m_genotype_files.size() > 0);
    m_unfiltered_marker_ct = 0;
    std::ifstream bimfile;
    std::string prev_chr = "";
    int chr_code = 0;
    int chr_index = 0;
    bool chr_error = false, chr_sex_error = false;
    m_num_ambig = 0;
    std::vector<SNP> snp_info;
    std::unordered_set<std::string> dup_list;
    m_num_snp_per_file.resize(m_genotype_files.size());
    size_t cur_file = 0;
    for (auto&& prefix : m_genotype_files) {
        std::string bimname = prefix + ".bim";
        bimfile.open(bimname.c_str());
        if (!bimfile.is_open()) {
            std::string error_message =
                "Error: Cannot open bim file: " + bimname;
            throw std::runtime_error(error_message);
        }
        std::string line;
        int num_line = 0;
        while (std::getline(bimfile, line)) {
            misc::trim(line);
            if (line.empty()) continue;
            std::vector<std::string> token = misc::split(line);
            if (token.size() < 6) {
                std::string error_message =
                    "Error: Malformed bim file. Less than 6 column on "
                    "line: "
                    + std::to_string(num_line) + "\n";
                throw std::runtime_error(error_message);
            }
            // change them to upper case
            std::transform(token[+BIM::A1].begin(), token[+BIM::A1].end(),
                           token[+BIM::A1].begin(), ::toupper);
            std::transform(token[+BIM::A2].begin(), token[+BIM::A2].end(),
                           token[+BIM::A2].begin(), ::toupper);
            std::string chr = token[+BIM::CHR];
            // exclude SNPs that are not required
            if (!m_exclude_snp
                && m_snp_selection_list.find(token[+BIM::RS])
                       == m_snp_selection_list.end())
            {
                m_unfiltered_marker_ct++;
                m_num_snp_per_file[cur_file]++;
                num_line++;
                continue;
            }
            else if (m_exclude_snp
                     && m_snp_selection_list.find(token[+BIM::RS])
                            != m_snp_selection_list.end())
            {
                m_unfiltered_marker_ct++;
                m_num_snp_per_file[cur_file]++;
                num_line++;
                continue;
            }

            /** check if this is from a new chromosome **/
            if (chr.compare(prev_chr) != 0) {
                // only work on this if this is a new chromosome
                prev_chr = chr;
                if (m_chr_order.find(chr) != m_chr_order.end()) {
                    throw std::runtime_error("ERROR: SNPs on the same "
                                             "chromosome must be clustered "
                                             "together!");
                }
                m_chr_order[chr] = chr_index++;
                // get the chromosome codes
                chr_code = get_chrom_code_raw(chr.c_str());
                if (((const uint32_t) chr_code) > m_max_code)
                { // bigger than the maximum code, ignore it
                    if (!chr_error) {
                        // only print this if an error isn't previously given
                        std::string error_message =
                            "WARNING: SNPs with chromosome number larger "
                            "than "
                            + std::to_string(m_max_code) + "."
                            + " They will be ignored!\n";
                        std::cerr << error_message
                                  << std::endl; // currently avoid passing in
                                                // reporter here
                        chr_error = true;
                        m_unfiltered_marker_ct++;
                        m_num_snp_per_file[cur_file]++;
                        continue;
                    }
                    else if (!chr_sex_error
                             && (is_set(m_haploid_mask.data(), chr_code)
                                 || chr_code == m_xymt_codes[X_OFFSET]
                                 || chr_code == m_xymt_codes[Y_OFFSET]))
                    {
                        // we ignore Sex chromosomes and haploid chromosome

                        fprintf(stderr, "WARNING: Currently not support "
                                        "haploid chromosome and sex "
                                        "chromosomes\n");
                        chr_sex_error = true;
                        m_unfiltered_marker_ct++;
                        m_num_snp_per_file[cur_file]++;
                        continue;
                    }
                }
            }
            // now get other information of the SNP
            int loc = misc::convert<int>(token[+BIM::BP]);
            if (loc < 0) {
                fprintf(stderr, "ERROR: SNP with negative corrdinate: %s:%s\n",
                        token[+BIM::RS].c_str(), token[+BIM::BP].c_str());
                throw std::runtime_error(
                    "Please check you have the correct input");
            }
            // we really don't like duplicated SNPs right?
            // or a more gentle way will be to exclude the subsequent SNP
            if (m_existed_snps_index.find(token[+BIM::RS])
                != m_existed_snps_index.end())
            {
                dup_list.insert(token[+BIM::RS]);
                // throw std::runtime_error(
                //    "ERROR: Duplicated SNP ID detected!\n");
            }
            else if (ambiguous(token[+BIM::A1], token[+BIM::A2]))
            {
                m_num_ambig++;
                if (filter.keep_ambig) {
                    // keep it if user want to
                    m_existed_snps_index[token[+BIM::RS]] = snp_info.size();
                    snp_info.emplace_back(SNP(token[+BIM::RS], chr_code, loc,
                                              token[+BIM::A1], token[+BIM::A2],
                                              prefix, num_line));
                }
            }
            else
            {
                m_existed_snps_index[token[+BIM::RS]] = snp_info.size();
                snp_info.emplace_back(SNP(token[+BIM::RS], chr_code, loc,
                                          token[+BIM::A1], token[+BIM::A2],
                                          prefix, num_line));
            }
            m_unfiltered_marker_ct++; // add in the checking later on
            m_num_snp_per_file[cur_file]++;
            num_line++;
        }
        bimfile.close();
        cur_file++;
    }
    if (dup_list.size() != 0) {
        std::ofstream log_file_stream;
        std::string dup_name = out_prefix + ".valid";
        log_file_stream.open(dup_name.c_str());
        if (!log_file_stream.is_open()) {
            std::string error_message = "ERROR: Cannot open file: " + dup_name;
            throw std::runtime_error(error_message);
        }
        for (auto&& snp : m_existed_snps) {
            if (dup_list.find(snp.rs()) != dup_list.end()) continue;
            log_file_stream << snp.rs() << std::endl;
        }
        log_file_stream.close();
        std::string error_message =
            "ERROR: Duplicated SNP ID detected!.Valid SNP ID stored at "
            + dup_name + ". You can avoid this error by using --extract "
            + dup_name;
        throw std::runtime_error(error_message);
    }
    if (m_unfiltered_marker_ct > 2147483645) {
        throw std::runtime_error(
            "Error: PLINK does not suport more than 2^31 -3 variants. "
            "As we are using PLINK for some of our functions, we might "
            "encounter problem too. "
            "Sorry.");
    }
    return snp_info;
}

void BinaryPlink::check_bed()
{
    uint32_t uii = 0;
    int64_t llxx = 0;
    int64_t llyy = 0;
    int64_t llzz = 0;
    size_t cur_file = 0;

    uintptr_t unfiltered_sample_ct4 = (m_unfiltered_sample_ct + 3) / 4;
    for (auto&& prefix : m_genotype_files) {
        std::string bedname = prefix + ".bed";
        m_bed_file.open(bedname.c_str(), std::ios::binary);
        if (!m_bed_file.is_open()) {
            std::string error_message = "Cannot read bed file: " + bedname;
            throw std::runtime_error(error_message);
        }
        m_bed_file.seekg(0, m_bed_file.end);
        llxx = m_bed_file.tellg();
        if (!llxx) {
            throw std::runtime_error("Error: Empty .bed file.");
        }
        m_bed_file.seekg(0, m_bed_file.beg);
        // will let the g_textbuf stay for now
        char version_check[3];
        m_bed_file.read(version_check, 3);
        uii = m_bed_file.gcount();
        size_t marker_ct = m_num_snp_per_file[cur_file];
        llyy = ((uint64_t) unfiltered_sample_ct4) * marker_ct;
        llzz = ((uint64_t) m_unfiltered_sample_ct) * ((marker_ct + 3) / 4);
        bool sample_major = false;
        // compare only the first 3 bytes
        if ((uii == 3) && (!memcmp(version_check, "l\x1b\x01", 3))) {
            llyy += 3;
        }
        else if ((uii == 3) && (!memcmp(version_check, "l\x1b", 3)))
        {
            // v1.00 sample-major
            sample_major = true;
            llyy = llzz + 3;
            m_bed_offset = 2;
        }
        else if (uii && (*version_check == '\x01'))
        {
            // v0.99 SNP-major
            llyy += 1;
            m_bed_offset = 1;
        }
        else if (uii && (!(*version_check)))
        {
            // v0.99 sample-major
            sample_major = true;
            llyy = llzz + 1;
            m_bed_offset = 2;
        }
        else
        {
            // pre-v0.99, sample-major, no header bytes
            sample_major = true;
            if (llxx != llzz) {
                // probably not PLINK-format at all, so give this error instead
                // of "invalid file size"
                throw std::runtime_error(
                    "Error: Invalid header bytes in .bed file.");
            }
            llyy = llzz;
            m_bed_offset = 2;
        }
        if (llxx != llyy) {
            if ((*version_check == '#')
                || ((uii == 3) && (!memcmp(version_check, "chr", 3))))
            {
                throw std::runtime_error("Error: Invalid header bytes in PLINK "
                                         "1 .bed file.  (Is this a UCSC "
                                         "Genome\nBrowser BED file instead?)");
            }
            else
            {
                throw std::runtime_error("Error: Invalid .bed file size.");
            }
        }
        if (sample_major) {
            throw std::runtime_error(
                "Error: Currently do not support sample major format");
        }
        m_bed_file.close();
        cur_file++;
    }
}


void BinaryPlink::read_score(std::vector<Sample_lite>& current_prs_score,
                             size_t start_index, size_t end_bound,
                             const size_t region_index)
{
    uintptr_t final_mask = get_final_mask(m_sample_ct);
    // for array size
    uintptr_t unfiltered_sample_ctl = BITCT_TO_WORDCT(m_unfiltered_sample_ct);
    uintptr_t unfiltered_sample_ct4 = (m_unfiltered_sample_ct + 3) / 4;
    size_t num_included_samples = current_prs_score.size();

    m_cur_file = ""; // just close it
    if (m_bed_file.is_open()) {
        m_bed_file.close();
    }
    // index is w.r.t. partition, which contain all the information
    std::vector<uintptr_t> genotype(unfiltered_sample_ctl * 2, 0);
    for (size_t i_snp = start_index; i_snp < end_bound; ++i_snp) {
        // for each SNP
        if (m_cur_file.empty()
            || m_cur_file.compare(m_existed_snps[i_snp].file_name()) != 0)
        {
            // If we are processing a new file
            if (m_bed_file.is_open()) {
                m_bed_file.close();
            }
            m_cur_file = m_existed_snps[i_snp].file_name();
            std::string bedname = m_cur_file + ".bed";
            m_bed_file.open(bedname.c_str(), std::ios::binary);
            if (!m_bed_file.is_open()) {
                std::string error_message =
                    "ERROR: Cannot open bed file: " + bedname;
                throw std::runtime_error(error_message);
            }
        }
        // only read this SNP if it falls within our region of interest
        if (!m_existed_snps[i_snp].in(region_index)) continue;
        // current location of the snp in the bed file
        // allow for quick jumping
        // very useful for read score as most SNPs might not
        // be next to each other
        size_t cur_line = m_existed_snps[i_snp].snp_id();
        if (!m_bed_file.seekg(
                m_bed_offset + (cur_line * ((uint64_t) unfiltered_sample_ct4)),
                std::ios_base::beg))
        {
            throw std::runtime_error("ERROR: Cannot read the bed file!");
        }
        // loadbuf_raw is the temporary
        // loadbuff is where the genotype will be located
        if (load_and_collapse_incl(m_unfiltered_sample_ct, m_sample_ct,
                                   m_sample_include.data(), final_mask, false,
                                   m_bed_file, m_tmp_genotype.data(),
                                   genotype.data()))
        {
            throw std::runtime_error("ERROR: Cannot read the bed file!");
        }

        uintptr_t* lbptr = genotype.data();
        uint32_t uii = 0;
        uintptr_t ulii = 0;
        uint32_t ujj;
        uint32_t ukk;
        std::vector<size_t> missing_samples;
        std::vector<double> sample_genotype(num_included_samples);
        double stat = m_existed_snps[i_snp].stat();
        bool flipped = m_existed_snps[i_snp].is_flipped();
        uint32_t sample_idx = 0;

        int aa = 0, aA = 0, AA = 0;
        size_t nmiss = 0;
        do
        {
            ulii = ~(*lbptr++);
            if (uii + BITCT2 > m_unfiltered_sample_ct) {
                ulii &= (ONELU << ((m_unfiltered_sample_ct & (BITCT2 - 1)) * 2))
                        - ONELU;
            }
            while (ulii) {
                ujj = CTZLU(ulii) & (BITCT - 2);
                ukk = (ulii >> ujj) & 3;
                sample_idx = uii + (ujj / 2);
                if (ukk == 1 || ukk == 3) // Because 01 is coded as missing
                {
                    // 3 is homo alternative
                    // int flipped_geno = snp_list[snp_index].geno(ukk);
                    if (sample_idx < num_included_samples) {
                        int g = (ukk == 3) ? 2 : ukk;
                        switch (g)
                        {
                        case 0: aa++; break;
                        case 1: aA++; break;
                        case 2: AA++; break;
                        }
                        sample_genotype[sample_idx] = g;
                    }
                }
                else // this should be 2
                {
                    missing_samples.push_back(sample_idx);
                    nmiss++;
                }
                ulii &= ~((3 * ONELU) << ujj);
            }
            uii += BITCT2;
        } while (uii < num_included_samples);

        if (num_included_samples - nmiss == 0) {
            m_existed_snps[i_snp].invalidate();
            continue;
        }
        // due to the way the binary code works, the aa will always be 0
        // added there just for fun tbh
        aa = num_included_samples - nmiss - aA - AA;
        assert(aa >= 0);
        if (flipped) {
            int temp = aa;
            aa = AA;
            AA = temp;
        }
        if (m_model == +MODEL::HETEROZYGOUS) {
            // 010
            aa += AA;
            AA = 0;
        }
        else if (m_model == +MODEL::DOMINANT)
        {
            // 011;
            aA += AA;
            AA = 0;
        }
        else if (m_model == +MODEL::RECESSIVE)
        {
            // 001
            aa += aA;
            aA = AA;
            AA = 0;
        }
        double maf = ((double) (aA + AA * 2)
                      / ((double) (num_included_samples - nmiss)
                         * 2.0)); // MAF does not count missing
        double center_score = stat * maf;
        size_t num_miss = missing_samples.size();
        size_t i_missing = 0;
        for (size_t i_sample = 0; i_sample < num_included_samples; ++i_sample) {
            if (i_missing < num_miss && i_sample == missing_samples[i_missing])
            {
                if (m_scoring == SCORING::MEAN_IMPUTE)
                    current_prs_score[i_sample].prs += center_score;
                if (m_scoring != SCORING::SET_ZERO)
                    current_prs_score[i_sample].num_snp++;

                i_missing++;
            }
            else
            { // not missing sample
                if (m_scoring == SCORING::CENTER) {
                    // if centering, we want to keep missing at 0
                    current_prs_score[i_sample].prs -= center_score;
                }
                int g = (flipped) ? fabs(sample_genotype[i_sample] - 2)
                                  : sample_genotype[i_sample];
                if (m_model == +MODEL::HETEROZYGOUS) {
                    g = (g == 2) ? 0 : g;
                }
                else if (m_model == +MODEL::RECESSIVE)
                {
                    g = std::max(0, g - 1);
                }
                else if (m_model == +MODEL::DOMINANT)
                {
                    g = (g == 2) ? 1 : g;
                }
                current_prs_score[i_sample].prs += g * stat * 0.5;
                current_prs_score[i_sample].num_snp++;
            }
        }
    }
}

void BinaryPlink::snp_filtering(Reporter& reporter)
{
    uintptr_t final_mask = get_final_mask(m_sample_ct);
    // for array size
    uintptr_t unfiltered_sample_ctl = BITCT_TO_WORDCT(m_unfiltered_sample_ct);
    uintptr_t unfiltered_sample_ct4 = (m_unfiltered_sample_ct + 3) / 4;
    size_t num_included_samples = m_sample_ct;
    size_t num_maf_filter = 0;
    size_t num_geno_filter = 0;
    m_cur_file = ""; // just close it
    if (m_bed_file.is_open()) {
        m_bed_file.close();
    }
    // index is w.r.t. partition, which contain all the information
    std::vector<uintptr_t> genotype(unfiltered_sample_ctl * 2, 0);
    std::vector<size_t> valid_index;
    valid_index.reserve(m_existed_snps.size());
    for (size_t i_snp = 0; i_snp < m_existed_snps.size(); ++i_snp) {
        auto&& snp = m_existed_snps[i_snp];
        if (m_cur_file.empty() || m_cur_file.compare(snp.file_name()) != 0) {
            // If we are processing a new file
            if (m_bed_file.is_open()) {
                m_bed_file.close();
            }
            m_cur_file = snp.file_name();
            std::string bedname = m_cur_file + ".bed";
            m_bed_file.open(bedname.c_str(), std::ios::binary);
            if (!m_bed_file.is_open()) {
                std::string error_message =
                    "ERROR: Cannot open bed file: " + bedname;
                throw std::runtime_error(error_message);
            }
        }
        size_t cur_line = snp.snp_id();
        if (!m_bed_file.seekg(
                m_bed_offset + (cur_line * ((uint64_t) unfiltered_sample_ct4)),
                std::ios_base::beg))
        {
            throw std::runtime_error("ERROR: Cannot read the bed file!");
        }
        if (load_and_collapse_incl(m_unfiltered_sample_ct, m_sample_ct,
                                   m_sample_include.data(), final_mask, false,
                                   m_bed_file, m_tmp_genotype.data(),
                                   genotype.data()))
        {
            throw std::runtime_error("ERROR: Cannot read the bed file!");
        }
        uintptr_t* lbptr = genotype.data();
        uint32_t uii = 0;
        uintptr_t ulii = 0;
        uint32_t ujj;
        uint32_t ukk;
        uint32_t sample_idx = 0;
        int aA = 0, AA = 0;
        size_t nmiss = 0;
        do
        {
            ulii = ~(*lbptr++);
            if (uii + BITCT2 > m_unfiltered_sample_ct) {
                ulii &= (ONELU << ((m_unfiltered_sample_ct & (BITCT2 - 1)) * 2))
                        - ONELU;
            }
            while (ulii) {
                ujj = CTZLU(ulii) & (BITCT - 2);
                ukk = (ulii >> ujj) & 3;
                sample_idx = uii + (ujj / 2);
                if (ukk == 1 || ukk == 3) // Because 01 is coded as missing
                {
                    // 3 is homo alternative
                    // int flipped_geno = snp_list[snp_index].geno(ukk);
                    if (sample_idx < num_included_samples) {
                        int g = (ukk == 3) ? 2 : ukk;
                        switch (g)
                        {
                        case 1: aA++; break;
                        case 2: AA++; break;
                        }
                    }
                }
                else // this should be 2
                {
                    nmiss++;
                }
                ulii &= ~((3 * ONELU) << ujj);
            }
            uii += BITCT2;
        } while (uii < num_included_samples);

        if (num_included_samples - nmiss == 0) {
            continue;
        }
        double maf = ((double) (aA + AA * 2)
                      / ((double) (num_included_samples - nmiss)
                         * 2.0)); // MAF does not count missing
        maf = (maf > 0.5) ? 1 - maf : maf;
        double geno = (double) nmiss / (double) num_included_samples;
        if (filter.filter_geno && geno > filter.geno) {
            num_geno_filter++;
            continue;
        }
        else if (filter.filter_maf && maf > filter.maf)
        {
            num_maf_filter++;
            continue;
        }
        valid_index.push_back(i_snp);
    }
    if (valid_index.size() != m_existed_snps.size())
    { // only do this if we need to remove some SNPs
        // we assume exist_index doesn't have any duplicated index

        int start = (valid_index.empty()) ? -1 : valid_index.front();
        int end = start;
        std::vector<SNP>::iterator last = m_existed_snps.begin();
        ;
        for (auto&& ind : valid_index) {
            if (ind == start || ind - end == 1)
                end = ind; // try to perform the copy as a block
            else
            {
                std::copy(m_existed_snps.begin() + start,
                          m_existed_snps.begin() + end + 1, last);
                last += end + 1 - start;
                start = ind;
                end = ind;
            }
        }
        if (!valid_index.empty()) {
            std::copy(m_existed_snps.begin() + start,
                      m_existed_snps.begin() + end + 1, last);
            last += end + 1 - start;
        }
        m_existed_snps.erase(last, m_existed_snps.end());
    }
    m_existed_snps_index.clear();
    // now m_existed_snps is ok and can be used directly
    size_t vector_index = 0;
    // we do it here such that the m_existed_snps is sorted correctly
    for (auto&& cur_snp : m_existed_snps) // should be in the correct order
    {
        m_existed_snps_index[cur_snp.rs()] = vector_index++;
    }
    // Suggest that we want to release memory
    // but this is only a suggest as this is non-binding request
    // Proper way of releasing memory will be to do swarp. Yet that
    // might lead to out of scrope or some other error here?
    m_existed_snps.shrink_to_fit();
    std::string message = "";
    if (num_geno_filter > 0) {
        message.append(std::to_string(num_geno_filter)
                       + " SNP(s) filtered based on genotype missingness\n");
    }
    if (num_maf_filter > 0) {
        message.append(std::to_string(num_maf_filter)
                       + " SNP(s) filtered based on MAF filtering\n");
    }
    message.append(std::to_string(m_existed_snps.size())
                   + " total SNPs remained after filtering\n\n");
    reporter.report(message);
}
