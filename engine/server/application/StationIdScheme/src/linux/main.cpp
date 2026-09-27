// ======================================================================
//
// main.cpp (stationIdScheme)
//
// Labels a login database with the rule its station ids were derived
// with (table station_id_scheme), and re-keys a database from the legacy
// gcc64 rule to the gcc32 rule. The LoginServer will not start until the
// label exists. See game/server/database/tools/station_id_scheme/README.md.
//
//   stationIdScheme classify               < ids.tsv > label.sql
//   stationIdScheme assert gcc32|gcc64     < ids.tsv > label.sql
//   stationIdScheme rekey                  < ids.tsv > rekey.sql
//
// ids.tsv is the combined output of export.sql from every schema that holds
// login or cluster tables. A report goes to stderr; the SQL goes to stdout
// only when it may be applied. The tool never connects to the database.
//
// Each witness (the account name the game recorded for one of an id's
// characters) is judged on its own: it reproduces its id under gcc32 only,
// under gcc64 only, under both (numeric names), or under neither. Only an
// exclusive witness is evidence for a scheme, and a witness that fits both
// can never outweigh one that fits only one.
//
// classify records a label only from complete evidence: every station id
// has an exclusive witness for that scheme, no witness is exclusive to the
// other scheme, and no witness is unexplained. assert records the
// operator's statement as such, and is refused if any witness is exclusive
// to the other scheme. Neither replaces a recorded label.
//
// rekey moves every station id to the id its account has under gcc32, and
// labels the database gcc32 (evidence) in the same transaction. It writes
// the script only if every id is accounted for: each is either witnessed
// only by names that reproduce it under gcc64 alone and agree on the new
// id, or witnessed only by names that fit both rules (and stays). Any other
// id, any collision of a new id with another id, or a label other than
// gcc64 stops it, and nothing is written.
//
// ======================================================================

#include "sharedFoundation/FirstSharedFoundation.h"
#include "sharedFoundation/StationIdFromAccountName.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

// ======================================================================

namespace
{
	using StationIdFromAccountName::Algorithm;
	using StationIdFromAccountName::Gcc32Murmur2;
	using StationIdFromAccountName::Gcc64Murmur64A;

	struct Id
	{
		std::set<std::string> tables;
		std::set<std::string> witnesses;
		std::set<std::string> texts; // the id as the export printed it
		bool proven32 = false; // some witness reproduces the id under gcc32 only
		bool proven64 = false; // some witness reproduces the id under gcc64 only
	};

	struct Label
	{
		std::string scheme;
		std::string basis;
	};

	// Script objvars that hold a station id as an int. export.sql lists the
	// same names.
	char const * const cs_stationIdObjvars = "'player_structure.admin_all_characters', 'chronicles.quest_creator_station_id', 'manf.owner_station_id'";

	// Every station id column; export.sql lists the same columns.
	char const * const cs_stationIdColumns[][2] =
	{
		// login tables
		{ "account_info", "station_id" },
		{ "account_reward_events", "station_id" },
		{ "account_reward_items", "station_id" },
		{ "extra_character_slots", "station_id" },
		{ "feature_id_transactions", "station_id" },
		{ "purge_accounts", "station_id" },
		{ "swg_characters", "station_id" },
		// cluster tables
		{ "players", "station_id" },
		{ "player_objects", "station_id" },
		{ "accounts", "station_id" },
		{ "temp_characters", "station_id" },
		{ "account_map", "parent_id" },
		{ "account_map", "child_id" },
		{ "character_profile", "station_id" }
	};

	// account_extract is filled from outside the game (the account list the
	// purge process reads) and may hold a station id in its unsigned form.
	// purge_process folds user_id onto the stored form wherever it reads it;
	// export.sql and the rekey script fold it the same way.
	char const * const cs_accountExtract[2] = { "account_extract", "user_id" };

	int usage()
	{
		fprintf(stderr, "usage: stationIdScheme classify < ids.tsv > label.sql\n"
		                "       stationIdScheme assert gcc32|gcc64 < ids.tsv > label.sql\n"
		                "       stationIdScheme rekey [--check] < ids.tsv > rekey.sql\n");
		return 2;
	}

	// A station id is stored as the signed 32-bit value with the same bits;
	// accept that form or the unsigned one.
	bool parseStationId(std::string const & text, uint32 & id)
	{
		char * end = nullptr;
		errno = 0;
		long long const v = strtoll(text.c_str(), &end, 10);
		if (errno == ERANGE || end == text.c_str() || *end != '\0' || v < -2147483648LL || v > 4294967295LL)
			return false;
		id = static_cast<uint32>(static_cast<uint64>(v));
		return true;
	}

	// The form in which the database stores a station id, and the text of
	// an int objvar holding one: the signed 32-bit decimal.
	std::string storedForm(uint32 const id)
	{
		return std::to_string(static_cast<int32>(id));
	}

	std::string tablesOf(Id const & e)
	{
		std::string tables;
		for (auto const & t : e.tables)
			tables += (tables.empty() ? "" : ", ") + t;
		return tables;
	}

	std::string describe(uint32 const id, Id const & e)
	{
		return std::to_string(id) + " (signed " + storedForm(id) + ") in " + tablesOf(e);
	}

	// text as the contents of a SQL string literal
	std::string quoted(char const * const text)
	{
		std::string result;
		for (char const * c = text; *c; ++c)
			result += (*c == '\'') ? std::string("''") : std::string(1, *c);
		return result;
	}

	void printLabelSql(char const * scheme, char const * basis)
	{
		printf("whenever sqlerror exit failure rollback\n"
		       "insert into station_id_scheme (id, scheme, basis) values (1, '%s', '%s');\n"
		       "commit;\n"
		       "exit\n", scheme, basis);
	}

	// ----------------------------------------------------------------------

	// The script is one PL/SQL block per schema. It carries every station id
	// of the export mapped to its new id (itself if it stays), and moves each
	// stored id through that map. A stored id missing from the map means the
	// database changed after the export, so the block stops and rolls back.
	// A check script does every step and then rolls back, so that every schema
	// can be verified before any of them is changed.
	void printRekeySql(std::map<uint32, Id> const & ids, std::map<uint32, uint32> const & moves, bool const check)
	{
		if (check)
			printf("-- CHECK ONLY: this script makes every change below and then rolls back.\n"
			       "-- It commits nothing. Run it in every schema before the real script.\n"
			       "--\n");
		printf("-- Re-keys this database's station ids from the gcc64 rule to the gcc32 rule.\n"
		       "-- Written by 'stationIdScheme rekey' from an export; see README.md.\n"
		       "--\n"
		       "-- %zu station ids move; %zu fit both rules and stay.\n"
		       "--\n"
		       "-- Apply it in every schema that holds login, cluster or station-players\n"
		       "-- (character_profile) tables, the one\n"
		       "-- holding station_id_scheme last. In each schema it is one transaction:\n"
		       "-- on any error nothing in that schema changes. Once it has committed,\n"
		       "-- the only way back is the backup taken before.\n"
		       "\n"
		       "whenever sqlerror exit failure rollback\n"
		       "set serveroutput on size unlimited format wrapped\n"
		       "set feedback off\n"
		       "set verify off\n"
		       "set define off\n"
		       "\n"
		       "declare\n"
		       "\t-- Every station id in the export, mapped to its id after the re-key,\n"
		       "\t-- both in the stored form (signed 32-bit decimal text).\n"
		       "\ttype id_map is table of varchar2(11) index by varchar2(1000);\n"
		       "\ttarget id_map;\n"
		       "\texpected_ids constant pls_integer := %zu;\n"
		       "\tobjvar_names constant varchar2(200) := '%s';\n",
		       moves.size(), ids.size() - moves.size(), ids.size(), quoted(cs_stationIdObjvars).c_str());
		printf("\n"
		       "\t-- p_pairs: 'old:new old:new ...'\n"
		       "\tprocedure load(p_pairs varchar2) is\n"
		       "\t\tpos pls_integer := 1;\n"
		       "\t\tsep pls_integer;\n"
		       "\t\tcolon pls_integer;\n"
		       "\tbegin\n"
		       "\t\twhile pos <= length(p_pairs) loop\n"
		       "\t\t\tsep := instr(p_pairs, ' ', pos);\n"
		       "\t\t\tif sep = 0 then\n"
		       "\t\t\t\tsep := length(p_pairs) + 1;\n"
		       "\t\t\tend if;\n"
		       "\t\t\tcolon := instr(p_pairs, ':', pos);\n"
		       "\t\t\ttarget(substr(p_pairs, pos, colon - pos)) := substr(p_pairs, colon + 1, sep - colon - 1);\n"
		       "\t\t\tpos := sep + 1;\n"
		       "\t\tend loop;\n"
		       "\tend;\n"
		       "\n"
		       "\tfunction has_column(p_table varchar2, p_column varchar2) return boolean is\n"
		       "\t\tn number;\n"
		       "\tbegin\n"
		       "\t\tselect count(*) into n from user_tab_columns\n"
		       "\t\twhere table_name = upper(p_table) and column_name = upper(p_column);\n"
		       "\t\treturn n > 0;\n"
		       "\tend;\n"
		       "\n"
		       "\tfunction target_of(p_value varchar2, p_where varchar2) return varchar2 is\n"
		       "\tbegin\n"
		       "\t\tif not target.exists(p_value) then\n"
		       "\t\t\traise_application_error(-20273, p_where || ' holds station id ' || p_value ||\n"
		       "\t\t\t\t', which is not in the export this script was written from: the database changed after the export. ' ||\n"
		       "\t\t\t\t'Nothing was changed. Export again and rerun stationIdScheme rekey.');\n"
		       "\t\tend if;\n"
		       "\t\treturn target(p_value);\n"
		       "\tend;\n"
		       "\n"
		       "\t-- p_unsigned: the column may hold the unsigned form; it is folded onto\n"
		       "\t-- the stored (signed) form before the lookup, and written signed. A value\n"
		       "\t-- in neither form is not in the map, so it stops the script.\n"
		       "\tprocedure rekey_column(p_table varchar2, p_column varchar2, p_unsigned boolean default false) is\n"
		       "\t\tc sys_refcursor;\n"
		       "\t\trid urowid;\n"
		       "\t\tv varchar2(1000);\n"
		       "\t\tt varchar2(11);\n"
		       "\t\tn pls_integer := 0;\n"
		       "\tbegin\n"
		       "\t\tif not has_column(p_table, p_column) then\n"
		       "\t\t\treturn;\n"
		       "\t\tend if;\n"
		       "\t\tif p_unsigned then\n"
		       "\t\t\topen c for 'select rowid, to_char(case when ' || p_column || ' between 2147483648 and 4294967295 then ' || p_column || ' - 4294967296 else ' || p_column || ' end) from ' || p_table || ' where ' || p_column || ' is not null';\n"
		       "\t\telse\n"
		       "\t\t\topen c for 'select rowid, to_char(' || p_column || ') from ' || p_table || ' where ' || p_column || ' is not null';\n"
		       "\t\tend if;\n"
		       "\t\tloop\n"
		       "\t\t\tfetch c into rid, v;\n"
		       "\t\t\texit when c%%notfound;\n"
		       "\t\t\tt := target_of(v, p_table || '.' || p_column);\n"
		       "\t\t\tif t <> v then\n"
		       "\t\t\t\texecute immediate 'update ' || p_table || ' set ' || p_column || ' = to_number(:1) where rowid = :2' using t, rid;\n"
		       "\t\t\t\tn := n + 1;\n"
		       "\t\t\tend if;\n"
		       "\t\tend loop;\n"
		       "\t\tclose c;\n"
		       "\t\tdbms_output.put_line(p_table || '.' || p_column || ': ' || n || ' rows re-keyed');\n"
		       "\tend;\n"
		       "\n"
		       "\t-- The int objvars that hold station ids, in object_variables and in\n"
		       "\t-- every packed objects.objvar_<n>_* slot (type 0 is DynamicVariable::INT).\n"
		       "\tprocedure rekey_objvars is\n"
		       "\t\tc sys_refcursor;\n"
		       "\t\trid urowid;\n"
		       "\t\tv varchar2(1000);\n"
		       "\t\tname varchar2(500);\n"
		       "\t\tt varchar2(11);\n"
		       "\t\tn pls_integer;\n"
		       "\t\tslot pls_integer := 0;\n"
		       "\tbegin\n"
		       "\t\tif has_column('object_variables', 'value') and has_column('object_variable_names', 'name') then\n"
		       "\t\t\tn := 0;\n"
		       "\t\t\topen c for\n"
		       "\t\t\t\t'select v.rowid, v.value, n.name from object_variables v, object_variable_names n ' ||\n"
		       "\t\t\t\t'where v.name_id = n.id and n.name in (' || objvar_names || ') ' ||\n"
		       "\t\t\t\t'and v.type = 0 and nvl(v.detached, 0) = 0 and v.value is not null';\n"
		       "\t\t\tloop\n"
		       "\t\t\t\tfetch c into rid, v, name;\n"
		       "\t\t\t\texit when c%%notfound;\n"
		       "\t\t\t\tt := target_of(v, 'object_variables.value (' || name || ')');\n"
		       "\t\t\t\tif t <> v then\n"
		       "\t\t\t\t\texecute immediate 'update object_variables set value = :1 where rowid = :2' using t, rid;\n"
		       "\t\t\t\t\tn := n + 1;\n"
		       "\t\t\t\tend if;\n"
		       "\t\t\tend loop;\n"
		       "\t\t\tclose c;\n"
		       "\t\t\tdbms_output.put_line('object_variables.value: ' || n || ' objvars re-keyed');\n"
		       "\t\tend if;\n"
		       "\t\twhile has_column('objects', 'objvar_' || slot || '_value') loop\n"
		       "\t\t\tn := 0;\n"
		       "\t\t\topen c for\n"
		       "\t\t\t\t'select rowid, objvar_' || slot || '_value, objvar_' || slot || '_name from objects ' ||\n"
		       "\t\t\t\t'where objvar_' || slot || '_name in (' || objvar_names || ') ' ||\n"
		       "\t\t\t\t'and objvar_' || slot || '_type = 0 and objvar_' || slot || '_value is not null';\n"
		       "\t\t\tloop\n"
		       "\t\t\t\tfetch c into rid, v, name;\n"
		       "\t\t\t\texit when c%%notfound;\n"
		       "\t\t\t\tt := target_of(v, 'objects.objvar_' || slot || '_value (' || name || ')');\n"
		       "\t\t\t\tif t <> v then\n"
		       "\t\t\t\t\texecute immediate 'update objects set objvar_' || slot || '_value = :1 where rowid = :2' using t, rid;\n"
		       "\t\t\t\t\tn := n + 1;\n"
		       "\t\t\t\tend if;\n"
		       "\t\t\tend loop;\n"
		       "\t\t\tclose c;\n"
		       "\t\t\tif n > 0 then\n"
		       "\t\t\t\tdbms_output.put_line('objects.objvar_' || slot || '_value: ' || n || ' objvars re-keyed');\n"
		       "\t\t\tend if;\n"
		       "\t\t\tslot := slot + 1;\n"
		       "\t\tend loop;\n"
		       "\tend;\n"
		       "\n"
		       "\t-- Cell allow (3) and ban (4) lists hold an account as 'A:<station id>'\n"
		       "\t-- (CellPermissions).\n"
		       "\tprocedure rekey_permissions is\n"
		       "\t\tc sys_refcursor;\n"
		       "\t\trid urowid;\n"
		       "\t\tv varchar2(1000);\n"
		       "\t\tt varchar2(11);\n"
		       "\t\tn pls_integer := 0;\n"
		       "\tbegin\n"
		       "\t\tif not has_column('property_lists', 'value') then\n"
		       "\t\t\treturn;\n"
		       "\t\tend if;\n"
		       "\t\topen c for 'select rowid, substr(value, 3) from property_lists where list_id in (3, 4) and substr(value, 1, 2) = ''A:''';\n"
		       "\t\tloop\n"
		       "\t\t\tfetch c into rid, v;\n"
		       "\t\t\texit when c%%notfound;\n"
		       "\t\t\tt := target_of(v, 'property_lists.value (A:)');\n"
		       "\t\t\tif t <> v then\n"
		       "\t\t\t\texecute immediate 'update property_lists set value = :1 where rowid = :2' using 'A:' || t, rid;\n"
		       "\t\t\t\tn := n + 1;\n"
		       "\t\t\tend if;\n"
		       "\t\tend loop;\n"
		       "\t\tclose c;\n"
		       "\t\tdbms_output.put_line('property_lists.value (A:): ' || n || ' permissions re-keyed');\n"
		       "\tend;\n"
		       "\n"
		       "\t-- A column read folded (p_unsigned) can hold both forms of one id in\n"
		       "\t-- two rows. Re-keying would give both rows one value, so refuse first.\n"
		       "\tprocedure check_folded_unique(p_table varchar2, p_column varchar2) is\n"
		       "\t\tn number;\n"
		       "\tbegin\n"
		       "\t\tif not has_column(p_table, p_column) then\n"
		       "\t\t\treturn;\n"
		       "\t\tend if;\n"
		       "\t\texecute immediate 'select count(*) from (select 1 from ' || p_table ||\n"
		       "\t\t\t' group by case when ' || p_column || ' between 2147483648 and 4294967295 then ' || p_column || ' - 4294967296 else ' || p_column || ' end' ||\n"
		       "\t\t\t' having count(*) > 1)' into n;\n"
		       "\t\tif n > 0 then\n"
		       "\t\t\traise_application_error(-20277, p_table || '.' || p_column || ' holds ' || n ||\n"
		       "\t\t\t\t' station ids in both their signed and unsigned forms; keep one row for each and export again. Nothing was changed.');\n"
		       "\t\tend if;\n"
		       "\tend;\n"
		       "\n"
		       "\tprocedure check_label is\n"
		       "\t\tn number;\n"
		       "\tbegin\n"
		       "\t\tif not has_column('station_id_scheme', 'scheme') then\n"
		       "\t\t\t-- The login tables and the label live in one schema (update 272).\n"
		       "\t\t\tif has_column('account_info', 'station_id') then\n"
		       "\t\t\t\traise_application_error(-20276, 'this schema holds the login tables but no station_id_scheme table; apply update 272 first. Nothing was changed.');\n"
		       "\t\t\tend if;\n"
		       "\t\t\tdbms_output.put_line('no station_id_scheme table in this schema; the label is changed in the schema that has it');\n"
		       "\t\t\treturn;\n"
		       "\t\tend if;\n"
		       "\t\texecute immediate 'select count(*) from station_id_scheme where scheme <> ''gcc64''' into n;\n"
		       "\t\tif n > 0 then\n"
		       "\t\t\traise_application_error(-20274, 'station_id_scheme records a scheme other than gcc64; only a gcc64 or unlabelled database is re-keyed. Nothing was changed.');\n"
		       "\t\tend if;\n"
		       "\tend;\n"
		       "\n"
		       "\tprocedure set_label is\n"
		       "\tbegin\n"
		       "\t\tif not has_column('station_id_scheme', 'scheme') then\n"
		       "\t\t\treturn;\n"
		       "\t\tend if;\n"
		       "\t\texecute immediate 'delete from station_id_scheme';\n"
		       "\t\texecute immediate 'insert into station_id_scheme (id, scheme, basis) values (1, ''gcc32'', ''evidence'')';\n"
		       "\t\tdbms_output.put_line('station_id_scheme: gcc32 (evidence)');\n"
		       "\tend;\n"
		       "begin\n");

		// The map, twenty ids per line.
		int column = 0;
		for (auto const & i : ids)
		{
			auto const m = moves.find(i.first);
			uint32 const to = (m == moves.end()) ? i.first : m->second;
			printf("%s%s:%s", column == 0 ? "\tload('" : " ", storedForm(i.first).c_str(), storedForm(to).c_str());
			if (++column == 20)
			{
				printf("');\n");
				column = 0;
			}
		}
		if (column != 0)
			printf("');\n");

		printf("\tif target.count <> expected_ids then\n"
		       "\t\traise_application_error(-20275, 'the id map holds ' || target.count || ' ids, not ' || expected_ids || ': this script is damaged. Nothing was changed.');\n"
		       "\tend if;\n"
		       "\n"
		       "\tcheck_label;\n");
		printf("\tcheck_folded_unique('%s', '%s');\n", cs_accountExtract[0], cs_accountExtract[1]);
		for (auto const & c : cs_stationIdColumns)
			printf("\trekey_column('%s', '%s');\n", c[0], c[1]);
		printf("\trekey_column('%s', '%s', true);\n", cs_accountExtract[0], cs_accountExtract[1]);
		printf("\trekey_objvars;\n"
		       "\trekey_permissions;\n"
		       "\tset_label;\n");
		if (check)
			printf("\trollback;\n"
			       "\tdbms_output.put_line('check passed in this schema; rolled back, nothing was changed');\n");
		else
			printf("\tcommit;\n");
		printf("exception\n"
		       "\twhen others then\n"
		       "\t\trollback;\n"
		       "\t\traise;\n"
		       "end;\n"
		       "/\n"
		       "exit\n");
	}

	// ----------------------------------------------------------------------

	int rekey(std::map<uint32, Id> const & ids, std::vector<Label> const & labels, bool const check)
	{
		if (labels.size() > 1)
		{
			fprintf(stderr, "refused: the export holds %zu labels; export each schema once\n", labels.size());
			return 1;
		}
		if (!labels.empty() && labels.front().scheme != "gcc64")
		{
			fprintf(stderr, "refused: the database is labelled %s (%s); only a gcc64 or unlabelled database is re-keyed, so there is nothing to do\n", labels.front().scheme.c_str(), labels.front().basis.c_str());
			return 1;
		}

		std::map<uint32, uint32> moves; // old id -> new id
		std::map<uint32, std::string> moveWitnesses;
		std::map<uint32, std::set<uint32> > movesTo; // new id -> old ids
		std::vector<std::string> unwitnessed, notStored, proven32, unexplained, conflicting, collisions;

		for (auto const & i : ids)
		{
			uint32 const id = i.first;
			Id const & e = i.second;

			for (auto const & text : e.texts)
				if (text != storedForm(id))
					notStored.push_back("'" + text + "' for " + describe(id, e));

			if (e.witnesses.empty())
			{
				unwitnessed.push_back(describe(id, e));
				continue;
			}

			// Each witness gives the id the account has under gcc32, or
			// disqualifies the id.
			std::map<uint32, std::string> targets; // new id -> witnesses
			bool qualified = true;
			for (auto const & name : e.witnesses)
			{
				uint32 const id32 = StationIdFromAccountName::derive(name, Gcc32Murmur2);
				uint32 const id64 = StationIdFromAccountName::derive(name, Gcc64Murmur64A);
				if (id64 == id)
				{
					std::string & names = targets[id32];
					names += (names.empty() ? "" : ", ") + name;
				}
				else if (id32 == id)
				{
					proven32.push_back(name + " for " + describe(id, e));
					qualified = false;
				}
				else
				{
					unexplained.push_back(name + " for " + describe(id, e));
					qualified = false;
				}
			}
			// Report every reason an id cannot move, not only the first.
			if (targets.size() > 1)
			{
				std::string detail;
				for (auto const & t : targets)
					detail += (detail.empty() ? "" : "; ") + t.second + " -> " + std::to_string(t.first);
				conflicting.push_back(describe(id, e) + ": " + detail);
				continue;
			}
			if (!qualified)
				continue;
			uint32 const to = targets.begin()->first;
			if (to != id)
			{
				moves[id] = to;
				moveWitnesses[id] = targets.begin()->second;
				movesTo[to].insert(id);
			}
		}

		// Never merge: a new id may be neither another account's new id nor
		// any id the database already holds.
		for (auto const & m : movesTo)
		{
			if (m.second.size() > 1)
			{
				std::string olds;
				for (auto const old : m.second)
					olds += (olds.empty() ? "" : ", ") + std::to_string(old) + " (" + moveWitnesses[old] + ")";
				collisions.push_back(std::to_string(m.first) + " is the new id of " + olds);
			}
			auto const existing = ids.find(m.first);
			if (existing != ids.end())
			{
				uint32 const old = *m.second.begin();
				collisions.push_back(std::to_string(old) + " (" + moveWitnesses[old] + ") would move to " + describe(existing->first, existing->second) + ", which already holds an account");
			}
		}

		fprintf(stderr, "station ids: %zu\n", ids.size());
		fprintf(stderr, "  move (every witness fits gcc64 only, and they agree): %zu\n", moves.size());
		for (auto const & m : moves)
			fprintf(stderr, "    %u (signed %d) -> %u (signed %d): %s; in %s\n", m.first, static_cast<int32>(m.first), m.second, static_cast<int32>(m.second), moveWitnesses[m.first].c_str(), tablesOf(ids.find(m.first)->second).c_str());

		struct
		{
			char const * title;
			std::vector<std::string> const * lines;
		} const problems[] =
		{
			{ "not in the stored form (signed 32-bit decimal; run update 272 first)", &notStored },
			{ "without a witness", &unwitnessed },
			{ "witness fits gcc32 only", &proven32 },
			{ "witness reproduces neither rule", &unexplained },
			{ "witnesses disagree on the new id", &conflicting },
			{ "collisions", &collisions }
		};
		size_t problemCount = 0;
		for (auto const & p : problems)
		{
			fprintf(stderr, "  %s: %zu\n", p.title, p.lines->size());
			for (auto const & line : *p.lines)
				fprintf(stderr, "    %s\n", line.c_str());
			problemCount += p.lines->size();
		}

		if (problemCount > 0)
		{
			fprintf(stderr, "aborted: every station id must be re-keyable or the same under both rules, and no new id may collide; nothing written\n");
			return 1;
		}
		if (moves.empty())
		{
			fprintf(stderr, "nothing to re-key: no station id is proven gcc64; nothing written\n");
			return 1;
		}
		fprintf(stderr, "complete: %zu station ids move, %zu stay; review this report, back up, then apply the script\n", moves.size(), ids.size() - moves.size());
		printRekeySql(ids, moves, check);
		return 0;
	}
}

// ======================================================================

int main(int argc, char ** argv)
{
	if (argc < 2)
		return usage();
	std::string const command = argv[1];
	Algorithm asserted = Gcc32Murmur2;
	bool check = false;
	if (command == "assert")
	{
		if (argc != 3 || !StationIdFromAccountName::parseAlgorithm(argv[2], asserted))
			return usage();
	}
	else if (command == "rekey" && argc == 3 && std::string(argv[2]) == "--check")
		check = true;
	else if ((command != "classify" && command != "rekey") || argc != 2)
		return usage();

	std::map<uint32, Id> ids;
	std::vector<Label> labels;
	std::set<std::string> unexplained; // witness names matching neither scheme
	int badLines = 0;

	std::string line;
	int lineNumber = 0;
	while (std::getline(std::cin, line))
	{
		++lineNumber;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;
		size_t const tab1 = line.find('\t');
		size_t const tab2 = (tab1 == std::string::npos) ? std::string::npos : line.find('\t', tab1 + 1);
		if (tab2 != std::string::npos && line.compare(0, tab1, "label") == 0)
		{
			labels.push_back(Label{line.substr(tab1 + 1, tab2 - tab1 - 1), line.substr(tab2 + 1)});
			continue;
		}
		uint32 id = 0;
		// A third tab means a field held one (an account name can), so the
		// fields cannot be told apart.
		if (tab2 == std::string::npos || line.find('\t', tab2 + 1) != std::string::npos || !parseStationId(line.substr(0, tab1), id))
		{
			fprintf(stderr, "line %d is not <station id> TAB <name> TAB <table>: %s\n", lineNumber, line.c_str());
			++badLines;
			continue;
		}
		Id & entry = ids[id];
		entry.tables.insert(line.substr(tab2 + 1));
		entry.texts.insert(line.substr(0, tab1));
		// Derive from exactly the name the LoginServer derives from.
		std::string const name = StationIdFromAccountName::normalizeAccountName(line.substr(tab1 + 1, tab2 - tab1 - 1));
		if (name.empty())
			continue;
		entry.witnesses.insert(name);
		bool const is32 = StationIdFromAccountName::derive(name, Gcc32Murmur2) == id;
		bool const is64 = StationIdFromAccountName::derive(name, Gcc64Murmur64A) == id;
		if (is32 && !is64)
			entry.proven32 = true;
		if (is64 && !is32)
			entry.proven64 = true;
		if (!is32 && !is64)
			unexplained.insert(name + " (" + std::to_string(id) + ")");
	}

	if (badLines > 0)
	{
		fprintf(stderr, "%d unreadable lines; nothing written\n", badLines);
		return 1;
	}

	if (command == "rekey")
		return rekey(ids, labels, check);

	int unwitnessed = 0, proven32 = 0, proven64 = 0, contradicted = 0, unproven = 0;
	for (auto const & i : ids)
	{
		Id const & e = i.second;
		if (e.witnesses.empty())
			++unwitnessed;
		else if (e.proven32 && e.proven64)
			++contradicted;
		else if (e.proven32)
			++proven32;
		else if (e.proven64)
			++proven64;
		else
			++unproven; // witnessed only by names that fit both schemes
	}
	bool const any32 = (proven32 + contradicted) > 0;
	bool const any64 = (proven64 + contradicted) > 0;

	fprintf(stderr, "station ids: %zu\n", ids.size());
	fprintf(stderr, "  proven gcc32 (a witness fits gcc32 only): %d\n", proven32);
	fprintf(stderr, "  proven gcc64 (a witness fits gcc64 only): %d\n", proven64);
	fprintf(stderr, "  proven both ways (witnesses disagree): %d\n", contradicted);
	fprintf(stderr, "  witnessed only by names that fit both (numeric): %d\n", unproven);
	fprintf(stderr, "  without a witness: %d\n", unwitnessed);
	fprintf(stderr, "witness names reproducing neither: %zu\n", unexplained.size());
	for (auto const & u : unexplained)
		fprintf(stderr, "  %s\n", u.c_str());
	for (auto const & i : ids)
		if (i.second.witnesses.empty())
			fprintf(stderr, "  no witness: %s\n", describe(i.first, i.second).c_str());

	if (!labels.empty())
	{
		fprintf(stderr, "refused: the database is already labelled %s (%s); a recorded label is never replaced, except by a verified re-key\n", labels.front().scheme.c_str(), labels.front().basis.c_str());
		return 1;
	}

	if (command == "classify")
	{
		if (ids.empty())
		{
			fprintf(stderr, "no station ids: recording gcc32 (new). If this database has accounts, the export came from the wrong schema; do not apply label.sql.\n");
			printLabelSql("gcc32", "new");
			return 0;
		}
		if (any32 && any64)
		{
			fprintf(stderr, "the evidence is contradictory: both schemes are proven, so the accounts were created under both rules; nothing recorded. One set of accounts must be re-keyed before the database can be labelled.\n");
			return 1;
		}
		bool const complete = unwitnessed == 0 && unproven == 0 && unexplained.empty();
		if (complete && any32)
		{
			fprintf(stderr, "complete evidence for gcc32\n");
			printLabelSql("gcc32", "evidence");
			return 0;
		}
		if (complete && any64)
		{
			fprintf(stderr, "complete evidence for gcc64\n");
			printLabelSql("gcc64", "evidence");
			return 0;
		}
		fprintf(stderr, "the evidence is incomplete; nothing recorded. If you know which rule created this database's accounts, use 'assert'.\n");
		return 1;
	}

	// assert
	char const * const scheme = StationIdFromAccountName::getAlgorithmName(asserted);
	bool const provenOther = (asserted == Gcc32Murmur2) ? any64 : any32;
	if (provenOther)
	{
		fprintf(stderr, "refused: %d station ids are proven under the other scheme; %s cannot be asserted\n", ((asserted == Gcc32Murmur2) ? proven64 : proven32) + contradicted, scheme);
		return 1;
	}
	fprintf(stderr, "recording %s as asserted by the operator\n", scheme);
	printLabelSql(scheme, "asserted");
	return 0;
}

// ======================================================================
