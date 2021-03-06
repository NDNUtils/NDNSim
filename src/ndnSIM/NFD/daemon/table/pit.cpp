/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2014-2016,  Regents of the University of California,
 *                           Arizona Board of Regents,
 *                           Colorado State University,
 *                           University Pierre & Marie Curie, Sorbonne University,
 *                           Washington University in St. Louis,
 *                           Beijing Institute of Technology,
 *                           The University of Memphis.
 *
 * This file is part of NFD (Named Data Networking Forwarding Daemon).
 * See AUTHORS.md for complete list of NFD authors and contributors.
 *
 * NFD is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NFD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NFD, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pit.hpp"
#include <type_traits>

#include <boost/concept/assert.hpp>
#include <boost/concept_check.hpp>
#include <type_traits>
#include <iostream>

using namespace std;


namespace nfd {
namespace pit {

#if HAVE_IS_MOVE_CONSTRUCTIBLE
static_assert(std::is_move_constructible<DataMatchResult>::value,
              "DataMatchResult must be MoveConstructible");
#endif // HAVE_IS_MOVE_CONSTRUCTIBLE

} // namespace pit

// http://en.cppreference.com/w/cpp/concept/ForwardIterator
BOOST_CONCEPT_ASSERT((boost::ForwardIterator<Pit::const_iterator>));
// boost::ForwardIterator follows SGI standard http://www.sgi.com/tech/stl/ForwardIterator.html,
// which doesn't require DefaultConstructible
#ifdef HAVE_IS_DEFAULT_CONSTRUCTIBLE
static_assert(std::is_default_constructible<Pit::const_iterator>::value,
              "Pit::const_iterator must be default-constructible");
#else
BOOST_CONCEPT_ASSERT((boost::DefaultConstructible<Pit::const_iterator>));
#endif // HAVE_IS_DEFAULT_CONSTRUCTIBLE

Pit::Pit(NameTree& nameTree)
  : m_nameTree(nameTree)
  , m_nItems(0)
  , m_droppedPackets(0)
{
}

Pit::~Pit()
{
        //std::cout << "m_droppedPackets = " << m_droppedPackets << std::endl;
       // std::cout << "m_nameTree size = " << m_nameTree.size() << std::endl;
}


//FindOrInsert by Puid
std::pair<shared_ptr<pit::Entry>, bool>
Pit::findOrInsertByPuid(const Interest& interest, bool allowInsert)
{
  // ensure NameTree entry exists
  const Name& namePuid = interest.getProducerUid();
  bool isEndWithDigest = namePuid.size() > 0 && namePuid[-1].isImplicitSha256Digest();
  shared_ptr<name_tree::Entry> nte = m_nameTree.lookup(isEndWithDigest ? namePuid.getPrefix(-1) : namePuid);
  BOOST_ASSERT(nte != nullptr);
  size_t nteNameLen = nte->getPrefix().size();

  // check if PIT entry already exists
  // A pit entry size is 144.
  // total size 14400 for 100 entries.
  const std::vector<shared_ptr<pit::Entry>>& pitEntries = nte->getPitEntries();
  //cout << "Size of PIT entries : " << (sizeof(pit::Entry) * m_nItems) << endl;
  auto it = std::find_if(pitEntries.begin(), pitEntries.end(),
    [&interest, nteNameLen] (const shared_ptr<pit::Entry>& entry) -> bool {
      // initial part of the name is guaranteed to be the same
      BOOST_ASSERT(entry->getInterest().getProducerUid().compare(0, nteNameLen,
                   interest.getProducerUid(), 0, nteNameLen) == 0);
      // compare implicit digest (or its absence) only
      return entry->getInterest().getProducerUid().compare(nteNameLen, Name::npos,
                                                    interest.getProducerUid(), nteNameLen) == 0 &&
             entry->getInterest().getSelectors() == interest.getSelectors();
    });

  if (it != pitEntries.end()) {
    return {*it, false};
  }
  
  //Newly added coded to drop Interests 
  auto entry = make_shared<pit::Entry>(interest);

  if((sizeof(pit::Entry) * m_nItems) < 506000){  // 1 entry = 144 byte
    nte->insertPitEntry(entry);
    m_nItems++;
  } else {
    m_droppedPackets++;
    return {nullptr, false};
  }

  /*if((sizeof(pit::Entry) * m_nItems) < 14400){
    nte->insertPitEntry(entry);
    m_nItems++;
  } else {
    allowInsert = false;
  }*/


  if (!allowInsert) {
    return {nullptr, true};
  }

/*
  auto entry = make_shared<pit::Entry>(interest);
  nte->insertPitEntry(entry);
  m_nItems++;*/
  
  /*
  if((sizeof(pit::Entry) * m_nItems) < 14400){
        auto entry = *pitEntries.end();
        erase(entry);
  }*/


  return {entry, true};
}



/*pit::DataMatchResult
Pit::findAllDataMatches(const Data& data) const
{
  auto&& ntMatches = m_nameTree.findAllMatches(data.getName(),
    [] (const name_tree::Entry& entry) { return entry.hasPitEntries(); });

  pit::DataMatchResult matches;
  for (const name_tree::Entry& nte : ntMatches) {
    for (const shared_ptr<pit::Entry>& pitEntry : nte.getPitEntries()) {
      if (pitEntry->getInterest().matchesData(data))
        matches.emplace_back(pitEntry);
    }
  }

  return matches;
}*/

//find all data matches by producer Uid
pit::DataMatchResult
Pit::findAllDataMatchesByPuid(const Data& data) const
{
  auto&& ntMatches = m_nameTree.findAllMatches(data.getProducerUid(),
    [] (const name_tree::Entry& entry) { return entry.hasPitEntries(); });

  pit::DataMatchResult matches;
  for (const name_tree::Entry& nte : ntMatches) {
    for (const shared_ptr<pit::Entry>& pitEntry : nte.getPitEntries()) {
      if (pitEntry->getInterest().matchesDataByPuid(data))
        matches.emplace_back(pitEntry);
    }
  }

  return matches;
}



void
Pit::erase(shared_ptr<pit::Entry> pitEntry)
{
  shared_ptr<name_tree::Entry> nameTreeEntry = pitEntry->m_nameTreeEntry;
  BOOST_ASSERT(static_cast<bool>(nameTreeEntry));

  nameTreeEntry->erasePitEntry(pitEntry);
  m_nameTree.eraseEntryIfEmpty(nameTreeEntry);

  --m_nItems;
}

Pit::const_iterator
Pit::begin() const
{
  return const_iterator(m_nameTree.fullEnumerate(
    [] (const name_tree::Entry& entry) { return entry.hasPitEntries(); }).begin());
}

} // namespace nfd
