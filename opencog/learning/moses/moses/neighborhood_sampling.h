/** neighborhood_sampling.h ---
 *
 * Copyright (C) 2010 OpenCog Foundation
 *
 * Author: Moshe Looks, Nil Geisweiller, Xiaohui Liu
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _OPENCOG_NEIGHBORHOOD_SAMPLING_H
#define _OPENCOG_NEIGHBORHOOD_SAMPLING_H

#include <iostream>
#include <algorithm>
#include <limits>

#include <boost/math/special_functions/binomial.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include <opencog/util/dorepeat.h>
#include <opencog/util/lazy_random_selector.h>

#include "using.h"
#include "../eda/initialization.h"
#include "../representation/instance_set.h"
#include "../moses/types.h"

namespace opencog { namespace moses {

using boost::math::binomial_coefficient;
using boost::numeric_cast;
using boost::numeric::positive_overflow;
using std::numeric_limits;

/**
 * This procedure generates the initial deme randomly
 *
 * @param fs  the deme
 * @param n   the size of deme
 * @param out deme iterator (where to store the instance)
 * @param end deme iterator, containing the end iterator,
 *            necessary to check that 'out' does not go out
 *            of the deme if it does so then an assert is raised
 */
template<typename Out>
void generate_initial_sample(const field_set& fs, int n, Out out, Out end,
                             RandGen& rng)
{
    dorepeat(n) {

        instance inst(fs.packed_width());

        randomize(fs, inst, rng);

        // Bias towards the exemplar instance
        for (field_set::bit_iterator it = fs.begin_bits(inst);
             it != fs.end_bits(inst); ++it)
            if (rng.randbool())
                *it = false;
        for (field_set::disc_iterator it = fs.begin_disc(inst);
             it != fs.end_disc(inst); ++it)
            if (rng.randbool())
                *it = 0;

        //add it
        OC_ASSERT(out != end);  // to avoid invalid memory write
        *out++ = inst;
    }
}

#if 0
Deprecated code -- this algo doesnt really make sense.

 * XXX Doing this to a contin really doesnt make sense: when altering
 * a contin, we want to take bigger or smaller steps, and not just
 * twiddle random bits in the contin.

/**
 * This routine modifies the instance 'inst' so that the new instance
 * lies at a Hamming distance from the input instance.  The Hamming
 * distance here is taken as the number of raw fields changed to encode
 * the contin.
 *
 * For example, if the contin[it.idx()] is encoded with depth = 4,
 * as (L R S S), then the neighbors at distance = 1 are (R R S S),
 * (L L S S), (L R L S), (L S S S) and (L R R S).  This routine will
 * randomly chose one of these.
 *
 * @param fs   deme
 * @param inst the instance will be modified is contin encoded with distance
 *             equal to n
 * @param it   the contin iterator of the instance
 * @param dist the Hamming distance by which the contin will be modified.
 * @param rng  the random generator
 */
inline void hamming_contin_neighbor(const field_set& fs,
                                     instance& inst,
                                     field_set::contin_iterator it,
                                     unsigned dist, RandGen& rng)
{
    size_t begin = fs.contin_to_raw_idx(it.idx());
    size_t length = fs.contin_length(inst, it.idx());
    size_t depth = fs.contin()[it.idx()].depth;
    // a random_selector is used not to pick up twice the same idx.
    // The max idx corresponds either to the first Stop, or, in case
    // there is no Stop, the last disc (i.e. either Left or Right)
    lazy_random_selector select(std::min(length + 1, depth), rng);

    for (unsigned i = dist; i > 0; i--) {
        size_t r = select();
        field_set::disc_iterator itr = fs.begin_raw(inst);
        itr += begin + r;
        // case inst at itr is Stop
        if (*itr == field_set::contin_spec::Stop) {
            *itr = rng.randbool() ?
                field_set::contin_spec::Left:
                field_set::contin_spec::Right;
            length++;
            select.reset_range(std::min(length + 1, depth));
        }
        // case inst at itr is Left or Right
        else {
            // whether r corresponds to the last Left or Right disc
            bool before_stop = r + 1 == length;
            // whether we allow to turn it to stop
            bool can_be_stop = i <= select.count_n_free();
            if (before_stop && can_be_stop && rng.randbool()) {
                *itr = field_set::contin_spec::Stop;
                select.reset_range(--length);
            } else
                *itr = field_set::contin_spec::switchLR(*itr);
        }
    }
}
#endif

// Twiddle one contin bit only.
// Only two choices available: change it to stop, or flip it.
// @itr points to a (pseudo-) bit in the contin (as a raw field).
inline void twiddle_contin_bit(field_set::disc_iterator itr,
                               RandGen& rng)
{
    if (rng.randbool())
    {
        *itr = field_set::contin_spec::Stop;
    }
    else
    {
        // Switch left and right.
        bool is_left = (*itr == field_set::contin_spec::Left);
        *itr = is_left ?
            field_set::contin_spec::Right:
            field_set::contin_spec::Left;
    }
}

/**
 * This routine modifies the instance 'inst' so that the new instance
 * has the n'th-from-last significant bit changed.
 *
 * For example, if the contin[it.idx()] is encoded with depth = 4,
 * as [L R S S], then the neighbors worth considering are [L S S S],
 * (a decrease of significant bits), [L L S S] (same number of bits,
 * but direction changed), [L R L S], and [L R R S] (increase the
 * number of significant bits).  This routine will randomly chose one
 * of these four possibiliities.
 *
 * @todo: in order to better approximate the real-number metric, we
 * should also consider the neighbor [LRRRRR..] of [RLLLLL...]
 *
 * @param fs   deme
 * @param inst the instance to be modified
 * @param it   the contin iterator pointing into the instance
 * @param dist the distance to consider.
 * @param rng  the random generator
 */
inline void generate_contin_neighbor(const field_set& fs,
                                     instance& inst,
                                     field_set::contin_iterator it,
                                     unsigned dist, RandGen& rng)
{
    size_t begin = fs.contin_to_raw_idx(it.idx());
    size_t length = fs.contin_length(inst, it.idx());
    size_t depth = fs.contin()[it.idx()].depth;

    // If length is depth, then no unused pseudo-bits; twiddle the
    // last bit only.
    if (depth == length)
    {
        field_set::disc_iterator itr = fs.begin_raw(inst);
        itr += begin + length;
        twiddle_contin_bit(itr, rng);
    }
    else
    {
        // If length is not equal to depth, then its less than depth,
        // and there is room at the end of the contin to change a less
        // significant bit.
        field_set::disc_iterator itr = fs.begin_raw(inst);
        if (rng.randbool())
        {
            itr += begin + length;
            twiddle_contin_bit(itr, rng);
        }
        else
        {
            // There is a stop bit here. Change it to L or R.
            itr += begin + length + 1;
            if (rng.randbool())
            {
               *itr = field_set::contin_spec::Right;
            }
            else
            {
               *itr = field_set::contin_spec::Left;
            }
        }
    }

    // Recurse, if need be.
    // Note that there is some risk that the old instance will be
    // re-created by this, and so there will be no effective change.
    // However, the extra cpu cycles needed to avoid this does not
    // seem to be worth the effort, right?
    if (1 < dist)
    {
        generate_contin_neighbor(fs, inst, it, dist-1, rng);
    }
}

/**
 * Sample 'sample_size' instances from the neighborhood that surrounds
 * the central instance, at a distance 'dist' from the center. That is,
 * sample 'sample_size' instances which have 'dist' different knob
 * settings than the given instance.
 *
 * @todo: term algebra fields are ignored for now
 *
 * @param fs            deme
 * @param dist          distance
 * @param sample_size   number of instances to be generated
 * @param out           deme where to store the instances
 * @param end           deme iterator, containing the end iterator,
 *                      necessary to check that 'out' does not go out
 *                      of the deme if it does so then an assert is raised
 * @param rng           the random generator
 * @param center_inst   the center instance
 */
template<typename Out>
void sample_from_neighborhood(const field_set& fs, unsigned dist,
                              unsigned sample_size, Out out, Out end,
                              RandGen& rng,
                              const instance & center_inst)
{
    OC_ASSERT(center_inst.size() == fs.packed_width(),
              "Please make sure that the center_inst"
              " have the same size with the field_set");

    unsigned dim = fs.dim_size();

    OC_ASSERT(dist <= dim,
              "the sampling distance %u"
              " cannot be greater than the field dimension %u", dist, dim);

    dorepeat(sample_size) {

        instance new_inst(center_inst);
        lazy_random_selector select(dim, rng);

        for (unsigned i = 1; i <= dist; ) {
            size_t r = select();
            field_set::bit_iterator itb = fs.begin_bits(new_inst);
            field_set::disc_iterator itd = fs.begin_disc(new_inst);
            field_set::contin_iterator itc = fs.begin_contin(new_inst);
            // modify bit
            if (r < fs.n_bits()) {
                itb += r;
                *itb = !(*itb);
                i++;
            // modify disc
            } else if (r >= fs.n_bits() && (r < (fs.n_bits() + fs.n_disc_fields()))) {
                itd += r - fs.n_bits();
                disc_t temp = 1 + rng.randint(itd.multy() - 1);
                if ( *itd == temp)
                    *itd = 0;
                else
                    *itd = temp;
                i++;
            // modify contin
            } else if ( r >= (fs.n_bits() + fs.n_disc_fields())) {
                //cout << "i = " << i << "  r = " << r << endl;
                itc += r - fs.n_bits() - fs.n_disc_fields();
                // @todo: now the distance is 1, choose the distance
                // of contin possibly different than 1
                generate_contin_neighbor(fs, new_inst, itc, 1, rng);
                i++;
            }
        }
        OC_ASSERT(out != end); // to avoid invalid memory write
        *out++ = new_inst;
        // cout << "********** Added instance:" << fs.stream(new_inst) << endl;
    }
}


/**
 * Sample 'sample_size' instances from the neighborhood of the zero
 * instance, at a distance 'dist' from zero.. That is, sample
 * 'sample_size' instances which have 'dist' different knobs set to
 * non-zero values.
 *
 * @param fs            deme
 * @param dist          distance
 * @param sample_size   number of instances to be generated
 * @param out           deme iterator (where to store the instances)
 * @param end           deme iterator, containing the end iterator,
 *                      necessary to check that 'out' does not go out
 *                      of the deme if it does so then an assert is raised
 */
template<typename Out>
void sample_from_neighborhood(const field_set& fs, unsigned dist,
                              unsigned sample_size, Out out, Out end,
                              RandGen& rng)
{
    instance inst(fs.packed_width());

    // Reset all fields (zero them out).
    // contin and term algebra fields are ignored. XXX Why?
    // Don't we want to start with all-zero contins ??? XXX fixme...
    for (field_set::bit_iterator it = fs.begin_bits(inst);
            it != fs.end_bits(inst); ++it)
        *it = false;

    for (field_set::disc_iterator it = fs.begin_disc(inst);
            it != fs.end_disc(inst); ++it)
        *it = 0;

    sample_from_neighborhood(fs, dist, sample_size, out, end, rng, inst);
}


/**
 * Generates instances at distance 'dist' from an instance considered
 * as center (often the exemplar but not necessarily).
 * It calls a recursive function vary_n_knobs which varies
 * instance fields one by one (in all possible ways)
 *
 * @param fs            deme
 * @param dist          distance
 * @param out           deme (where to store the instances)
 * @param end           deme iterator, containing the end iterator,
 *                      necessary to check that 'out' does not go out
 *                      of the deme if it does so then an assert is raised
 * @param center_inst   the center instance
 */
template<typename Out>
void generate_all_in_neighborhood(const field_set& fs, unsigned dist,
                                  Out out, Out end,
                                  const instance& center_inst)
{
    OC_ASSERT(center_inst.size() == fs.packed_width(),
              "the size of center_instance should be equal to the width of fs");
    vary_n_knobs(fs, center_inst, dist, 0, out, end);
}


/**
 * Generates instances at distance 'dist' from the exemplar
 * (i.e., with n elements changed from 0 from the exemplar)
 * It calls a recursive function vary_n_knobs which varies
 * instance fields one by one (in all possible ways)
 *
 * @param fs   deme
 * @param dist distance
 * @param out  deme (where to store the instances)
 * @param end  deme iterator, containing the end iterator,
 *             necessary to check that 'out' does not go out
 *             of the deme if it does so then an assert is raised
 */
template<typename Out>
void generate_all_in_neighborhood(const field_set& fs,
                                  unsigned dist, Out out, Out end)
{
    instance inst(fs.packed_width());
    generate_all_in_neighborhood(fs, dist, out, end, inst);
}

/**
 * vary_n_knobs -- Generate all possible instances, centered on 'inst',
 * up to a distance 'dist' from 'inst'.  This will vary all knobs that
 * have a raw index equal or greater than the 'starting_index', in
 * all possible ways.
 *
 * The current algorithm is recursive: it explores all possible settings
 * by recursing on 'dist' and on the field 'starting_index'.  Recursion
 * terminates when 'dist' drops to zero, and when 'starting_index' moves
 * past the maximum number of fields.
 *
 * The deme must be large enough to hold all generated instances.
 *
 * This function is the main work-horse for generate_all_in_neighborhood().
 *
 * XXX TODO: the current algo could be speeded up a fair bit, cutting
 * out some of the if tests, and the final recursive call.
 *
 * @todo: term algebra is ignored for the moment.
 *
 * @param fs              deme
 * @param inst            exemplar
 * @param dist            distance
 * @param starting_index  position of a field to be varied
 * @param out             deme iterator (where to store the instances)
 * @param end             deme iterator, containing the end iterator.
 *                        This is used to verify that the write to the deme
 *                        does not go out of bounds.
 * @return the out iterator pointing to the element after the last insertion
 */
template<typename Out>
Out vary_n_knobs(const field_set& fs,
                 const instance& inst,
                 unsigned dist,
                 unsigned starting_index,
                 Out out, Out end)
{
    // Record the current instance.
    if (dist == 0) {
        OC_ASSERT(out != end);  // to avoid invalid memory write
        *out++ = inst;
        return out;
    }

    instance tmp_inst = inst;

    // term knobs.
    if ((fs.begin_term_raw_idx() <= starting_index) &&
        (starting_index < fs.end_term_raw_idx()))
    {
        // @todo: handle term algebras XXX
        out = vary_n_knobs(fs, tmp_inst, dist,
                           starting_index + fs.end_term_raw_idx(),
                           out, end);
    }
    // contin knobs
    else
    if ((fs.begin_contin_raw_idx() <= starting_index) &&
        (starting_index < fs.end_contin_raw_idx()))
    {
        // Modify the contin knob pointed by itr, then recurse on
        // starting_index and dist.
        field_set::contin_iterator itc = fs.begin_contin(tmp_inst);
        size_t contin_idx = fs.raw_to_contin_idx(starting_index);
        itc += contin_idx;

        // The 'depth' is the max possible size of this contin field,
        // while length is the actual size, for this instance.
        size_t depth = fs.contin()[itc.idx()].depth;
        size_t length = fs.contin_length(tmp_inst, contin_idx);

        field_set::disc_iterator itr = fs.begin_raw(tmp_inst);
        itr += starting_index;
        size_t relative_raw_idx = starting_index - fs.contin_to_raw_idx(contin_idx);

        // case tmp_inst at itr is Stop
        if (*itr == field_set::contin_spec::Stop) {
            // Assume that this is the first stop encountered for this
            // contin field.  Skip straight to the next contin field.
            size_t next_contin = starting_index + depth - relative_raw_idx;
            out = vary_n_knobs(fs, tmp_inst, dist, next_contin, out, end);

            // Turn this stop pseudo-bit to Left, and Right, and recurse
            // to next.
            *itr = field_set::contin_spec::Left;
            out = vary_n_knobs(fs, tmp_inst, dist - 1, starting_index + 1, out, end);
            *itr = field_set::contin_spec::Right;
            out = vary_n_knobs(fs, tmp_inst, dist - 1, starting_index + 1, out, end);
        }
        // case tmp_inst at itr is Left or Right
        else
        {
            // Recursive call, moved for one position
            out = vary_n_knobs(fs, tmp_inst, dist, starting_index + 1, out, end);
            // Left<->Right
            *itr = field_set::contin_spec::switchLR(*itr);
            out = vary_n_knobs(fs, tmp_inst, dist - 1, starting_index + 1, out, end);

            // Set all remaining 'pseudo-bits' in this contin field to Stop,
            // remRLs is the number of remaining non-Stop pseudo-bits,
            // including this one.
            unsigned remRLs = length - relative_raw_idx;
            if (remRLs <= dist) {
                for(; relative_raw_idx < length; --length, ++itr) {
                    // Stop
                    *itr = field_set::contin_spec::Stop;
                }
                // Skip straight to the next contin field.
                size_t next_contin = starting_index + depth - relative_raw_idx;
                out = vary_n_knobs(fs, tmp_inst, dist - remRLs, next_contin,
                                   out, end);
            }
        }
    }

    // Discrete knobs
    else
    if ((fs.begin_disc_raw_idx() <= starting_index) &&
        (starting_index < fs.end_disc_raw_idx()))
    {
        field_set::disc_iterator itd = fs.begin_disc(tmp_inst);
        itd += fs.raw_to_disc_idx(starting_index);
        disc_t tmp_val = *itd;

        // Recursive call, moved for one position.
        out = vary_n_knobs(fs, tmp_inst, dist, starting_index + 1, out, end);

        // modify the disc and recursive call, moved for one position
        for (unsigned i = 1; i <= itd.multy() - 1; ++i) {
            // Vary to all legal values.  The neighborhood should
            // not equal to itself, so if it is same, set it to 0.
            if (tmp_val == i)
                *itd = 0;
            else
                *itd = i;
            out = vary_n_knobs(fs, tmp_inst, dist - 1, starting_index + 1, out, end);
        }
    }
    // Single-bit knobs
    else
    if ((fs.begin_bit_raw_idx() <= starting_index) &&
        (starting_index < fs.end_bit_raw_idx()))
    {
        field_set::bit_iterator itb = fs.begin_bits(tmp_inst);
        itb += starting_index - fs.begin_bit_raw_idx();

        // Recursive call, moved for one position.
        out = vary_n_knobs(fs, tmp_inst, dist, starting_index + 1, out, end);

        // Modify tmp_inst at itb, change to the opposite value.
        *itb = !(*itb);

        // Recursive call, moved for one position.
        out = vary_n_knobs(fs, tmp_inst, dist - 1, starting_index + 1, out, end);
    }
    else
    {
        // The current recursive algo used here will over-run the end
        // of the array by one. This is normal. Do nothing. If 'dist'
        // was zero, the very first statement at top takes care of
        // things for us.
    }
    return out;
}

/**
 * safe_binomial_coefficient -- compute the binomial coefficient
 *
 * This is algo is "safe" in that it attempts to deal with numeric
 * overflow in computing the binomial coefficient, so that overflows
 * are clamped to max, instead of wrapping over, or throwing.
 */
inline deme_size_t
safe_binomial_coefficient(unsigned k, unsigned n)
{
    deme_size_t res;
    double noi_db = binomial_coefficient<double>(k, n);
    try {
        res = numeric_cast<deme_size_t>(noi_db);
    } catch (positive_overflow&) {
        res = numeric_limits<deme_size_t>::max();
    }
    return res;
}

/**
 * Count the number of neighbors surrounding instance 'inst', at a
 * distance of 'dist' or less, starting with knob 'starting_index'.
 *
 * Given an instance 'inst', the 'neighborhood at distance one' is the
 * set of all instances with one changed knob setting.  The set with
 * two changed knob settings is the neighborhood at distance two, etc.
 *
 * This routine is implemented with a recursive algorithm, and so
 * counting starts with knobs numbered with 'starting_index' or higher.
 * To avoid wasting cpu cycles, counting is stopped when 'max_count'
 * is exceeded.
 *
 * @param fs              deme
 * @param inst            centeral instance
 * @param dist            distance
 * @param starting_index  position of a field to be varied
 * @param max_count       stop counting once this value is reached.
 */
inline deme_size_t
count_neighborhood_size_from_index(const field_set& fs,
                                 const instance& inst,
                                 unsigned dist,
                                 unsigned starting_index,
                                 deme_size_t max_count
                                 = numeric_limits<deme_size_t>::max())
{
    if (dist == 0)
        return 1;

    deme_size_t number_of_instances = 0;

    // terms
    if ((fs.begin_term_raw_idx() <= starting_index) &&
        (starting_index < fs.end_term_raw_idx()))
    {
        // @todo: handle term algebras
        number_of_instances =
            count_neighborhood_size_from_index(fs, inst, dist,
                                             starting_index + fs.end_term_raw_idx(),
                                             max_count);
    }

    // contins
    else
    if ((fs.begin_contin_raw_idx() <= starting_index) &&
        (starting_index < fs.end_contin_raw_idx()))
    {
        field_set::const_contin_iterator itc = fs.begin_contin(inst);

        size_t contin_idx = fs.raw_to_contin_idx(starting_index);
        OC_ASSERT(starting_index - fs.contin_to_raw_idx(contin_idx) == 0);

        itc += contin_idx;
        int depth = fs.contin()[itc.idx()].depth;
        int length = fs.contin_length(inst, contin_idx);

        // Calculate number_of_instances for each possible distance i
        // of the current contin.
        for (int i = 0; i <= min((int)dist, depth); ++i) {

            // Number of instances for this contin, at distance i.
            unsigned cni = 0;

            // Count combinations when Left or Right are switched and
            // added after Stop, where j represents the number of
            // Left or Right added after Stop.
            for (int j = max(0, i-length); j <= min(i, depth-length); ++j)
                cni += (unsigned) binomial_coefficient<double>(length, i-j)
                    * pow2(j);

            // Count combinations when Left or Right are switched and
            // removed before Stop, where j represents the number of
            // removed Left or Right before Stop.
            if (i <= length)
                for (int j = 1; j <= min(i, length); ++j)
                    cni += (unsigned) binomial_coefficient<double>(length-j, i-j);

            // Recursive call.
            number_of_instances +=
                cni * count_neighborhood_size_from_index(fs, inst, dist-i,
                                                       starting_index + depth,
                                                       max_count);
            // Stop prematurely if above max_count.
            if (number_of_instances > max_count)
                return number_of_instances;
        }
    }

    // discs
    else
    if ((fs.begin_disc_raw_idx() <= starting_index) &&
        (starting_index < fs.end_disc_raw_idx()))
    {
        // Recursive call, moved for one position.
        number_of_instances =
            count_neighborhood_size_from_index(fs, inst, dist,
                                             starting_index + 1, max_count);

        // stop prematurely if above max_count
        if (number_of_instances > max_count)
            return number_of_instances;

        // count all legal values of the knob
        field_set::const_disc_iterator itd = fs.begin_disc(inst);
        itd += starting_index - fs.begin_disc_raw_idx();

        number_of_instances +=
            (itd.multy() - 1)
            * count_neighborhood_size_from_index(fs, inst, dist - 1,
                                               starting_index + 1, max_count);
    }

    // bits
    else
    if ((fs.begin_bit_raw_idx() <= starting_index) &&
        (starting_index < fs.end_bit_raw_idx()))
    {

        // Since bits all have the same multiplicity (viz. 2), and are
        // the last in the field set, there is no need for recursive call.
        unsigned rb = fs.end_bit_raw_idx() - starting_index;
        if (dist <= rb)
            number_of_instances = safe_binomial_coefficient(rb, dist);
    }
    else
    {
        // Harmless; this recursive algo is desgined to over-run by
        // exactly one.
    }

    return number_of_instances;
}

/**
 * Count the number of neighbors surrounding instance 'inst', at a
 * distance of 'dist' or less.
 *
 * Given an instance 'inst', the 'neighborhood at distance one' is the
 * set of all instances with one changed knob setting.  The set with
 * two changed knob settings is the neighborhood at distance two, etc.
 *
 * To avoid wasting cpu cycles, counting is stopped when 'max_count'
 * is exceeded.
 *
 * @param fs              deme
 * @param inst            central instance
 * @param dist            distance
 * @param max_count       stop counting when the count exceeds this value.
 * @return                the size of the nieghborhood.
 */
inline deme_size_t count_neighborhood_size(const field_set& fs,
                                         const instance& inst,
                                         unsigned dist,
                                         deme_size_t max_count
                                         = numeric_limits<deme_size_t>::max())
{
    return count_neighborhood_size_from_index(fs, inst, dist, 0, max_count);
}

// For backward compatibility, like above but with null instance
inline deme_size_t count_neighborhood_size(const field_set& fs,
                                         unsigned dist,
                                         deme_size_t max_count
                                         = numeric_limits<deme_size_t>::max())
{
    instance inst(fs.packed_width());
    return count_neighborhood_size_from_index(fs, inst, dist, 0, max_count);
}

/// Fill the deme with at most number_of_new_instances, at distance
/// dist.  Return the actual number of new instances created (this
/// number is bounded by the possible neighbors at distance dist).
inline deme_size_t
sample_new_instances(deme_size_t total_number_of_neighbours,
                     deme_size_t number_of_new_instances,
                     deme_size_t current_number_of_instances,
                     const instance& center_inst,
                     instance_set<composite_score>& deme,
                     unsigned dist,
                     RandGen& rng)
{
    if (number_of_new_instances < total_number_of_neighbours) {
        // Resize the deme so it can take new instances.
        deme.resize(current_number_of_instances + number_of_new_instances);
        // Sample number_of_new_instances instances at
        // distance 'distance' from the exemplar.
        sample_from_neighborhood(deme.fields(), dist,
                                 number_of_new_instances,
                                 deme.begin() + current_number_of_instances,
                                 deme.end(), rng,
                                 center_inst);
    } else {
        number_of_new_instances = total_number_of_neighbours;
        // Resize the deme so it can take new instances
        deme.resize(current_number_of_instances + number_of_new_instances);
        // Add all instances on the distance dist from
        // the initial instance.
        generate_all_in_neighborhood(deme.fields(), dist,
                                     deme.begin() + current_number_of_instances,
                                     deme.end(),
                                     center_inst);
    }
    return number_of_new_instances;
}

/// Just like the above, but computes total_number_of_neighbours
/// instead of taking it argument.
//
inline deme_size_t
sample_new_instances(deme_size_t number_of_new_instances,
                     deme_size_t current_number_of_instances,
                     const instance& center_inst,
                     instance_set<composite_score>& deme,
                     unsigned dist,
                     RandGen& rng)
{
    // The number of all neighbours at the distance d (stops
    // counting when above number_of_new_instances).
    deme_size_t total_number_of_neighbours =
        count_neighborhood_size(deme.fields(), center_inst, dist,
                                number_of_new_instances);
    return sample_new_instances(total_number_of_neighbours,
                                number_of_new_instances,
                                current_number_of_instances,
                                center_inst, deme, dist, rng);
}

} // ~namespace moses
} // ~namespace opencog

#endif // _OPENCOG_NEIGHBORHOOD_SAMPLING_H
