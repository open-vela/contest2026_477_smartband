"""Guard against starting the showcase over a user's active session."""
import importlib.util
from pathlib import Path
import threading

import pytest

spec = importlib.util.spec_from_file_location('mooncat_demo', Path(__file__).parents[1]/'scripts/mooncat_demo.py')
demo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(demo)


@pytest.mark.parametrize('workout,phase', [(True, 0), (False, 1), (False, 3), (False, 4)])
def test_active_session_is_not_touched(workout, phase):
    class Board:
        def connect(self):
            return {'workout': workout, 'action': 0}
        def app(self):
            return {'workout': workout, 'tasks': [{'phase': phase}]}
        def __getattr__(self, name):
            raise AssertionError('Unexpected device operation: '+name)
    show = demo.Demo(Board(), threading.Event(), lambda *args: None)
    with pytest.raises(RuntimeError, match='进行中'):
        show.run()
    show.cleanup()
    assert not show.prepared


def test_invalid_state_stops_before_navigation():
    with pytest.raises(RuntimeError, match='格式'):
        demo.parse_app('MCAPP1\n0\n')
    with pytest.raises(RuntimeError, match='不完整'):
        demo.parse_app('MCAPP2\n123\n1\n')


def test_page_mismatch_is_not_ignored():
    class Board:
        def state(self):
            return {'page': 2, 'overlay': 7}
    show = demo.Demo(Board(), threading.Event(), lambda *args: None)
    with pytest.raises(RuntimeError, match='未到预期'):
        show.go(4)


def test_stop_prevents_new_navigation():
    stop = threading.Event()
    stop.set()
    show = demo.Demo(None, stop, lambda *args: None)
    with pytest.raises(demo.Stopped):
        show.go(1)
