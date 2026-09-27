-- Lists every station id persisted in this schema, one per line:
--   <station id> TAB <account name, if known> TAB <where it is stored>
-- and the label this schema records, if any:
--   label TAB <scheme> TAB <basis>
-- Run it in each schema that holds login, cluster or station-players
-- (character_profile) tables and give the combined output to
-- stationIdScheme (see README.md). Nothing is changed.

set serveroutput on size unlimited format wrapped
set feedback off
set heading off
set verify off
set echo off
whenever sqlerror exit failure

declare
	tab constant varchar2(1) := chr(9);

	-- Script objvars that hold a station id: int objvars (DynamicVariable::INT,
	-- type 0) whose value is the id's signed 32-bit decimal text. The rekey
	-- script (StationIdScheme main.cpp) moves the same list.
	objvar_names constant varchar2(200) :=
		'''player_structure.admin_all_characters'', ''chronicles.quest_creator_station_id'', ''manf.owner_station_id''';

	function has_column(p_table varchar2, p_column varchar2) return boolean is
		n number;
	begin
		select count(*) into n from user_tab_columns
		where table_name = upper(p_table) and column_name = upper(p_column);
		return n > 0;
	end;

	-- p_unsigned: the column may hold the unsigned form, which is printed
	-- folded onto the stored (signed) form, as purge_process reads it. Any
	-- other value is printed as it is, and stationIdScheme rejects it.
	procedure ids(p_table varchar2, p_column varchar2, p_unsigned boolean default false) is
		c sys_refcursor;
		v number;
	begin
		if not has_column(p_table, p_column) then
			return;
		end if;
		if p_unsigned then
			open c for 'select distinct case when ' || p_column || ' between 2147483648 and 4294967295 then ' || p_column || ' - 4294967296 else ' || p_column || ' end from ' || p_table || ' where ' || p_column || ' is not null';
		else
			open c for 'select distinct ' || p_column || ' from ' || p_table || ' where ' || p_column || ' is not null';
		end if;
		loop
			fetch c into v;
			exit when c%notfound;
			dbms_output.put_line(v || tab || tab || p_table || '.' || p_column);
		end loop;
		close c;
	end;

	-- Station ids held in script objvars, in both places an objvar is
	-- stored: a row of object_variables, or one of the packed
	-- objects.objvar_<n>_* slots. The value is printed exactly as stored.
	procedure objvar_ids is
		c sys_refcursor;
		v varchar2(1000);
		name varchar2(500);
		slot number := 0;
	begin
		if has_column('object_variables', 'value') and has_column('object_variable_names', 'name') then
			open c for
				'select distinct v.value, n.name from object_variables v, object_variable_names n ' ||
				'where v.name_id = n.id and n.name in (' || objvar_names || ') ' ||
				'and v.type = 0 and nvl(v.detached, 0) = 0 and v.value is not null';
			loop
				fetch c into v, name;
				exit when c%notfound;
				dbms_output.put_line(v || tab || tab || 'object_variables.value (' || name || ')');
			end loop;
			close c;
		end if;
		while has_column('objects', 'objvar_' || slot || '_value') loop
			open c for
				'select distinct objvar_' || slot || '_value, objvar_' || slot || '_name from objects ' ||
				'where objvar_' || slot || '_name in (' || objvar_names || ') ' ||
				'and objvar_' || slot || '_type = 0 and objvar_' || slot || '_value is not null';
			loop
				fetch c into v, name;
				exit when c%notfound;
				dbms_output.put_line(v || tab || tab || 'objects.objvar_' || slot || '_value (' || name || ')');
			end loop;
			close c;
			slot := slot + 1;
		end loop;
	end;

	-- Accounts on cell allow (3) and ban (4) lists: 'A:<station id>'
	-- (CellPermissions). The id text is printed exactly as stored.
	procedure permission_ids is
		c sys_refcursor;
		v varchar2(1000);
	begin
		if not has_column('property_lists', 'value') then
			return;
		end if;
		open c for 'select distinct substr(value, 3) from property_lists where list_id in (3, 4) and substr(value, 1, 2) = ''A:''';
		loop
			fetch c into v;
			exit when c%notfound;
			dbms_output.put_line(v || tab || tab || 'property_lists.value (A:)');
		end loop;
		close c;
	end;

	-- Witnesses: the account name the game recorded for each character that
	-- has entered the world (objvar system.accountUsername).
	procedure witnesses is
		c sys_refcursor;
		v number;
		name varchar2(1000);
		n number;
	begin
		if not has_column('players', 'station_id') then
			return;
		end if;
		select count(*) into n from user_views where view_name = 'OBJECT_VARIABLES_VIEW';
		if n = 0 then
			return;
		end if;
		open c for
			'select distinct p.station_id, v.value from players p, object_variables_view v ' ||
			'where v.object_id = p.character_object and v.name = ''system.accountUsername'' ' ||
			'and p.station_id is not null and v.value is not null';
		loop
			fetch c into v, name;
			exit when c%notfound;
			dbms_output.put_line(v || tab || name || tab || 'players.station_id');
		end loop;
		close c;
	end;

	-- The label, in the schema that holds station_id_scheme.
	procedure label is
		c sys_refcursor;
		scheme varchar2(16);
		basis varchar2(16);
	begin
		if not has_column('station_id_scheme', 'scheme') then
			return;
		end if;
		open c for 'select scheme, basis from station_id_scheme';
		loop
			fetch c into scheme, basis;
			exit when c%notfound;
			dbms_output.put_line('label' || tab || scheme || tab || basis);
		end loop;
		close c;
	end;
begin
	-- login tables
	ids('account_info', 'station_id');
	ids('account_reward_events', 'station_id');
	ids('account_reward_items', 'station_id');
	ids('extra_character_slots', 'station_id');
	ids('feature_id_transactions', 'station_id');
	ids('purge_accounts', 'station_id');
	ids('swg_characters', 'station_id');
	ids('account_extract', 'user_id', true);
	-- cluster tables
	ids('players', 'station_id');
	ids('player_objects', 'station_id');
	ids('accounts', 'station_id');
	ids('temp_characters', 'station_id');
	ids('account_map', 'parent_id');
	ids('account_map', 'child_id');
	ids('character_profile', 'station_id');
	-- script objvars
	objvar_ids;
	permission_ids;
	witnesses;
	label;
end;
/
exit
