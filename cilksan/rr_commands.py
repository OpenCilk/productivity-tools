import re

races = []
race_info = []
current_race = [-1, 0, 0]

def time_for_race(race_id):
    return race_info[race_id][0]

def addr_for_race(race_id):
    return race_info[race_id][1]

def get_time_for_current_race():
    if current_race[2] == 0:
        return time_for_race(current_race[0])
    else:
        return time_for_race(races[current_race[0]][current_race[1]])

class LoadRacesCommand(gdb.Command):
    """Load Cilksan-detected determinacy races."""

    def __init__(self):
        super(LoadRacesCommand, self).__init__('load-races',
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
    """Print loaded Cilksan-detected determinacy races."""

    def __init__(self):
        super(PrintRacesCommand, self).__init__('print-races',
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
    """Print loaded Cilksan-detected determinacy races."""

    def __init__(self):
        super(RaceInfoCommand, self).__init__('race-info',
                                              gdb.COMMAND_SUPPORT,
                                              gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        if current_race[0] == -1:
            print("No races recorded.  Use 'load-races <filename>' to load races from <filename>")
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
        for racer in races[race_to_examine]:
            print("  Races with ID", racer)
        print("Race on address", hex(addr_for_race(race_to_examine)))

        if view_current_race:
            print("seek-to-race targeting racing pair",
                  (race_to_examine, races[race_to_examine][current_race[1]]))
            if current_race[2] == 0:
                print("  first endpoint")
            else:
                print("  second endpoint")

RaceInfoCommand()

class SeekToRaceCommand(gdb.Command):
    """Seek to a specified Cilksan-detected determinacy race."""

    def __init__(self):
        super(SeekToRaceCommand, self).__init__('seek-to-race',
                                                gdb.COMMAND_RUNNING,
                                                gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        args = gdb.string_to_argv(arg)
        # If no arguments are given, seek to the current race.
        if len(args) < 1:
            if current_race[0] == -1:
                print("No races recorded.  Use 'load-races <filename>' to load races from <filename>")
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
    """Seek to the next recorded determinacy race."""

    def __init__(self):
        super(NextRaceCommand, self).__init__('next-race',
                                              gdb.COMMAND_RUNNING,
                                              gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        if len(races) == 0:
            print("No races recorded.  Use 'load-races <filename>' to load races from <filename>")
            return
        current_race[0] = current_race[0] + 1
        if current_race[0] == len(races):
            current_race[0] = 0
        current_race[1] = 0
        current_race[2] = 0
        gdb.execute('seek-user-time ' + str(get_time_for_current_race()))

NextRaceCommand()

class PrevRaceCommand(gdb.Command):
    """Seek to the previous recorded determinacy race."""

    def __init__(self):
        super(PrevRaceCommand, self).__init__('prev-race',
                                              gdb.COMMAND_RUNNING,
                                              gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        if len(races) == 0:
            print("No races recorded.  Use 'load-races <filename>' to load races from <filename>")
            return
        current_race[0] = current_race[0] - 1
        if current_race[0] < 0:
            current_race[0] = len(races) - 1
        current_race[1] = 0
        current_race[2] = 0
        gdb.execute('seek-user-time ' + str(get_time_for_current_race()))

PrevRaceCommand()

class ToggleRaceCommand(gdb.Command):
    """Toogle the current race: seek to the opposite racing pair."""

    def __init__(self):
        super(ToggleRaceCommand, self).__init__('toggle-race',
                                                gdb.COMMAND_RUNNING,
                                                gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        if len(races) == 0:
            print("No races recorded.  Use 'load-races <filename>' to load races from <filename>")
            return
        if current_race[2] == 0:
            current_race[2] = 1
        else:
            current_race[2] = 0
        gdb.execute('seek-user-time ' + str(get_time_for_current_race()))

ToggleRaceCommand()

class NextRacingPairCommand(gdb.Command):
    """Seek to the next instruction that races with current racing instruction."""

    def __init__(self):
        super(NextRacingPairCommand, self).__init__('next-racing-pair',
                                                    gdb.COMMAND_RUNNING,
                                                    gdb.COMPLETE_NONE, False)

    def invoke(self, arg, from_tty):
        if len(races) == 0:
            print("No races recorded.  Use 'load-races <filename>' to load races from <filename>")
            return
        current_race[1] = current_race[1] + 1
        if current_race[1] >= len(races[current_race[0]]):
            current_race[1] = 0
        if current_race[2] == 1:
            gdb.execute('seek-user-time ' + str(get_time_for_current_race()))

NextRacingPairCommand()
