import re

races = []
race_info = []
current_race = [-1, 0, 0]

no_races_error_msg = "No races recorded.  Use 'cilksan load-races FILENAME' to load races from FILENAME."

def time_for_race(race_id):
    return race_info[race_id][0]

def addr_for_race(race_id):
    return race_info[race_id][1]

def get_time_for_current_race():
    if current_race[2] == 0:
        return time_for_race(current_race[0])
    else:
        return time_for_race(races[current_race[0]][current_race[1]])

class CilksanPrefixCommand(gdb.Command):
    """Prefix for Cilksan-related commands for navigating between
determinacy races.

Each race is identified by a racing instruction I in the program,
which is given an integer ID from 0 up to the number of instructions
in the program involved in a determinacy race.

Each race I has an associated list of racers, which are instructions
that race with I.  Each instruction in the list of racers forms a
racing pair with I.

Each race also identifies the memory location that is raced on.

For navigating between races, these commands maintain a current race
target, which consists of:
- the current race I,
- the current racing pair (I, J), where J is among the racers of I, and
- an endpoint in that pair.
Navigation commands can operate relative to the current race target
and update that target.  The command `cilksan race-info` prints
information about the current race target."""

    def __init__(self):
        super(CilksanPrefixCommand, self).__init__('cilksan',
                                                   gdb.COMMAND_RUNNING,
                                                   gdb.COMPLETE_NONE, True)

CilksanPrefixCommand()

class LoadRacesCommand(gdb.Command):
    """Load determinacy races from a file.

Usage: cilksan load-races FILENAME

Load determinacy races from FILENAME."""

    def __init__(self):
        super(LoadRacesCommand, self).__init__('cilksan load-races',
                                               gdb.COMMAND_SUPPORT,
                                               gdb.COMPLETE_FILENAME, False)

    def invoke(self, arg, from_tty):
        races_dict = {}
        race_addrs_dict = {}
        with open (arg, 'r') as f:
            for line in f:
                parse = re.search("race\s+([0-9a-fA-F]+)\s+(\d+)\s+(\d+)", line)
                if parse is None:
                    print("Skipping \"" + line + "\": Failed to parse")
                    continue
                race_first  = int(parse.group(2))
                race_second = int(parse.group(3))
                race_addr   = int(parse.group(1), 16)
                # cilksan_races.append((race_first, race_second, race_addr))
                if race_first in races_dict:
                    races_dict[race_first].append(race_second)
                else:
                    races_dict[race_first] = [race_second]
                    race_addrs_dict[race_first] = race_addr

                if race_second in races_dict:
                    races_dict[race_second].append(race_first)
                else:
                    races_dict[race_second] = [race_first]
                    race_addrs_dict[race_second] = race_addr

        race_idx = {}
        sorted_races = sorted(races_dict)
        for idx, key in enumerate(sorted_races):
            race_info.append((key, race_addrs_dict[key]))
            race_idx[key] = idx

        for idx, key in enumerate(sorted_races):
            races.append([race_idx[k] for k in sorted(races_dict[key])])
        current_race[0] = 0

LoadRacesCommand()

class PrintRacesCommand(gdb.Command):
    """Print all loaded determinacy races."""

    def __init__(self):
        super(PrintRacesCommand, self).__init__('cilksan print-races',
                                                gdb.COMMAND_SUPPORT,
                                                gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        args = gdb.string_to_argv(arg)
        if len(args) > 0 and int(args[0]) < len(races):
            race_id = int(args[0])
            print(args[0], time_for_race(race_id), hex(addr_for_race(race_id)),
                  [(racer, time_for_race(racer)) for racer in races[race_id]])
        else:
            print(len(races), "race entries")
            for idx, race in enumerate(races):
                print(idx, race)

PrintRacesCommand()

class RaceInfoCommand(gdb.Command):
    """Get information about a race.

Usage: cilksan race-info [ID]

Print information about determinacy race ID.

If no ID is given, print information about the current race target."""

    def __init__(self):
        super(RaceInfoCommand, self).__init__('cilksan race-info',
                                              gdb.COMMAND_SUPPORT,
                                              gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        if current_race[0] == -1:
            print(no_races_error_msg)
            return
        args = gdb.string_to_argv(arg)
        view_current_race = True
        race_to_examine = current_race[0]
        if len(args) > 0:
            view_current_race = False
            race_to_examine = int(args[0])
            if race_to_examine < 0 or race_to_examine >= len(races):
                print("Invalid race ID", race_to_examine)
                return

        print(len(races), "total race entries (2 per racing pair)")
        print("Race ID", race_to_examine)
        print("Race on address", hex(addr_for_race(race_to_examine)))
        for racer in races[race_to_examine]:
            print("  Races with ID", racer)

        if view_current_race:
            print("Current race target:\n  racing pair",
                  (race_to_examine, races[race_to_examine][current_race[1]]))
            if current_race[2] == 0:
                print("  first endpoint")
            else:
                print("  second endpoint")

RaceInfoCommand()

class SeekToRaceCommand(gdb.Command):
    """Seek program execution to a racing instruction.

Usage: cilksan seek-to-race [ID [N]]

Seek to determinacy race ID and instruction N in the list of racers,
and update the current race and current target.  If N is 0, seek to
the racing instruction identified by ID.  Otherwise, seek to the Nth
instruction in the list of racers.

If ID is given and N is not given, then N is assumed to be 0.

If ID is not given, seeks to the current race target."""

    def __init__(self):
        super(SeekToRaceCommand, self).__init__('cilksan seek-to-race',
                                                gdb.COMMAND_RUNNING,
                                                gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        args = gdb.string_to_argv(arg)
        # If no arguments are given, seek to the current race.
        if len(args) < 1:
            if current_race[0] == -1:
                print(no_races_error_msg)
                return
            gdb.execute('seek-user-time ' + str(get_time_for_current_race()))
            return

        # Otherwise seek to the specified race and position, and update the
        # current race.
        race_id = int(args[0])
        if race_id >= len(races):
            print("Invalid race number " + str(race_id))
            return
        race_pos = 0
        if len(args) > 1:
            race_pos = int(args[1])
            if race_pos > len(races[race_id]):
                gdb.write("Invalid race position " + str(race_pos))
                return
        current_race[0] = race_id
        if race_pos == 0:
            current_race[1] = 0
            current_race[2] = 0
        else:
            current_race[1] = race_pos-1
            current_race[2] = 1
        gdb.execute('seek-user-time ' + str(get_time_for_current_race()))

SeekToRaceCommand()

class NextRaceCommand(gdb.Command):
    """Seek program execution to the next race.

Usage: cilksan next-race

Seek to determinacy race ID+1, where ID identifies the current race.
Update the current race target."""

    def __init__(self):
        super(NextRaceCommand, self).__init__('cilksan next-race',
                                              gdb.COMMAND_RUNNING,
                                              gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        if len(races) == 0:
            print(no_races_error_msg)
            return
        current_race[0] = current_race[0] + 1
        if current_race[0] == len(races):
            current_race[0] = 0
        current_race[1] = 0
        current_race[2] = 0
        gdb.execute('seek-user-time ' + str(get_time_for_current_race()))

NextRaceCommand()

class PrevRaceCommand(gdb.Command):
    """Seek program execution to the previous race.

Usage: cilksan prev-race

Seek to determinacy race ID-1, where ID identifies the current race.
Update the current race target."""

    def __init__(self):
        super(PrevRaceCommand, self).__init__('cilksan prev-race',
                                              gdb.COMMAND_RUNNING,
                                              gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        if len(races) == 0:
            print(no_races_error_msg)
            return
        current_race[0] = current_race[0] - 1
        if current_race[0] < 0:
            current_race[0] = len(races) - 1
        current_race[1] = 0
        current_race[2] = 0
        gdb.execute('seek-user-time ' + str(get_time_for_current_race()))

PrevRaceCommand()

class FFToRaceCommand(gdb.Command):
    """Fast-forward program execution to the next race after this time.

Usage: cilksan ff-to-race

Seek to the next determinacy race after the current point in the replay.
Update the current race target."""

    def __init__(self):
        super(FFToRaceCommand, self).__init__('cilksan ff-to-race',
                                              gdb.COMMAND_RUNNING,
                                              gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        if len(races) == 0:
            print(no_races_error_msg)
            return
        # Get the current user time
        when_user_time = gdb.execute('when-user-time', False, True)
        parse = re.search("Current\s+user\s+time:\s+(-?\d+)", when_user_time)
        current_user_time = int(parse.group(1))

        # Binary search for the next race after the current user time
        start = 0
        end = len(races)
        while end - start > 1:
            mid = start + ((end - start) // 2)
            next_race_time = time_for_race(mid)
            if next_race_time <= current_user_time:
                start = mid
            else:
                end = mid
        if time_for_race(start) <= current_user_time:
            start = start + 1
        if start >= len(races):
            print("No races after this point in the replay.")
            return

        # Seek to the race
        next_race_time = time_for_race(start)
        current_race[0] = start
        current_race[1] = 0
        current_race[2] = 0
        gdb.execute('seek-user-time ' + str(next_race_time))

FFToRaceCommand()

class RWToRaceCommand(gdb.Command):
    """Rewind program execution to the previous race before this time.

Usage: cilksan rw-to-race

Seek to the previous determinacy race before the current point in the replay.
Update the current race target."""

    def __init__(self):
        super(RWToRaceCommand, self).__init__('cilksan rw-to-race',
                                              gdb.COMMAND_RUNNING,
                                              gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        if len(races) == 0:
            print(no_races_error_msg)
            return
        # Get the current user time
        when_user_time = gdb.execute('when-user-time', False, True)
        parse = re.search("Current\s+user\s+time:\s+(-?\d+)", when_user_time)
        current_user_time = int(parse.group(1))

        # Binary search for the next race after the current user time
        start = 0
        end = len(races)
        while end - start > 1:
            mid = start + ((end - start) // 2)
            prev_race_time = time_for_race(mid)
            if prev_race_time < current_user_time:
                start = mid
            else:
                end = mid
        if time_for_race(start) >= current_user_time:
            if start == 0:
                print("No races before this point in the replay.")
                return
            start = start - 1

        # Seek to the race
        next_race_time = time_for_race(start)
        current_race[0] = start
        current_race[1] = 0
        current_race[2] = 0
        gdb.execute('seek-user-time ' + str(next_race_time))

RWToRaceCommand()

class ToggleRaceCommand(gdb.Command):
    """Seek program execution to the opposite racing instruction.

Usage: cilksan toggle-race

Toogle the current race target: target the opposite endpoint of the
current racing pair, and seek to that instruction."""

    def __init__(self):
        super(ToggleRaceCommand, self).__init__('cilksan toggle-race',
                                                gdb.COMMAND_RUNNING,
                                                gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        if len(races) == 0:
            print(no_races_error_msg)
            return
        if current_race[2] == 0:
            current_race[2] = 1
        else:
            current_race[2] = 0
        gdb.execute('seek-user-time ' + str(get_time_for_current_race()))

ToggleRaceCommand()

class NextRacingPairCommand(gdb.Command):
    """Update the targeted racing pair in the current race target.

Usage: cilksan next-racing-pair

Target the next racing pair associated with the current race.  Seek to
that new target."""

    def __init__(self):
        super(NextRacingPairCommand, self).__init__('cilksan next-racing-pair',
                                                    gdb.COMMAND_RUNNING,
                                                    gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        if len(races) == 0:
            print(no_races_error_msg)
            return
        current_race[1] = current_race[1] + 1
        if current_race[1] >= len(races[current_race[0]]):
            current_race[1] = 0
        if current_race[2] == 1:
            gdb.execute('seek-user-time ' + str(get_time_for_current_race()))

NextRacingPairCommand()
